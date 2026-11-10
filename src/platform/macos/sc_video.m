/** @file src/platform/macos/sc_video.m
 * @brief ScreenCaptureKit capture with bounded buffering and explicit stop/error completion.
 */
#import "sc_video.h"

@implementation SCVideo {
  SCDisplay *_display;  ///< Selected native display.
  SCStream *_stream;  ///< Active ScreenCaptureKit stream.
  dispatch_queue_t _queue;  ///< Serial capture callbacks.
  dispatch_semaphore_t _finished;  ///< Signals completion to the streaming capture loop.
  FrameCallbackBlock _callback;  ///< Consumer of retained sample buffers.
  BOOL _stopping;  ///< Prevent duplicate stop requests.
}

- (id)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate {
  // AVVideo's designated initializer starts the legacy capture stack; initialize NSObject directly.
  self = [super init];
  if (!self) {
    return nil;
  }
  self.displayID = displayID;
  self.minFrameDuration = CMTimeMake(1, MAX(frameRate, 1));
  self.pixelFormat = kCVPixelFormatType_32BGRA;
  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
  if (!mode) {
    [self release];
    return nil;
  }
  self.frameWidth = (int) CGDisplayModeGetPixelWidth(mode);
  self.frameHeight = (int) CGDisplayModeGetPixelHeight(mode);
  CFRelease(mode);
  dispatch_semaphore_t ready = dispatch_semaphore_create(0);
  dispatch_retain(ready);
  NSMutableArray *selection = [[NSMutableArray alloc] init];
  [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                             onScreenWindowsOnly:NO
                                               completionHandler:^(SCShareableContent *content, NSError *error) {
                                                 if (!error) {
                                                   for (SCDisplay *display in content.displays) {
                                                     if (display.displayID == displayID) {
                                                       [selection addObject:display];
                                                       break;
                                                     }
                                                   }
                                                 }
                                                 dispatch_semaphore_signal(ready);
                                                 dispatch_release(ready);
                                               }];
  // The callback retains only its result container, not this capture object.
  if (!dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC))) {
    _display = [[selection firstObject] retain];
  }
  [selection release];
  dispatch_release(ready);
  if (!_display) {
    [self release];
    return nil;
  }
  _queue = dispatch_queue_create("io.arf.sunshine.capture", DISPATCH_QUEUE_SERIAL);
  return self;
}

- (dispatch_semaphore_t)capture:(FrameCallbackBlock)callback {
  [_stream release];
  [_callback release];
  if (_finished) {
    dispatch_release(_finished);
  }
  _finished = dispatch_semaphore_create(0);
  dispatch_semaphore_t finished = _finished;
  _callback = [callback copy];
  _stopping = NO;
  self.captureFailed = NO;
  SCContentFilter *filter = [[[SCContentFilter alloc] initWithDisplay:_display excludingWindows:@[]] autorelease];
  SCStreamConfiguration *config = [self streamConfiguration];
  _stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:self];
  NSError *error = nil;
  if (![_stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:_queue error:&error]) {
    self.captureFailed = YES;
    NSLog(@"Sunshine ScreenCaptureKit output setup failed: %@", error.localizedDescription);
    dispatch_semaphore_signal(_finished);
    return _finished;
  }
  [_stream startCaptureWithCompletionHandler:^(NSError *startError) {
    if (startError) {
      self.captureFailed = YES;
      NSLog(@"Sunshine ScreenCaptureKit capture failed: %@", startError.localizedDescription);
      dispatch_semaphore_signal(finished);
    }
  }];
  return _finished;
}

- (SCStreamConfiguration *)streamConfiguration {
  SCStreamConfiguration *config = [[[SCStreamConfiguration alloc] init] autorelease];
  config.width = self.frameWidth;
  config.height = self.frameHeight;
  config.pixelFormat = self.pixelFormat;
  config.minimumFrameInterval = self.minFrameDuration;
  config.queueDepth = 3;
  config.showsCursor = self.cursorVisible;
  if (@available(macOS 13.0, *)) {
    config.capturesAudio = NO;  // Core Audio retains ownership of device selection and host mute policy.
  }
  return config;
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
  if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sampleBuffer) || !CMSampleBufferGetImageBuffer(sampleBuffer) || _stopping) {
    return;
  }
  CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
  if (attachments && CFArrayGetCount(attachments)) {
    NSDictionary *metadata = (NSDictionary *) CFArrayGetValueAtIndex(attachments, 0);
    NSNumber *status = metadata[SCStreamFrameInfoStatus];
    if (status && status.integerValue != SCFrameStatusComplete) {
      return;
    }
  }
  if (_callback && !_callback(sampleBuffer)) {
    [self stopCapture];
  }
}

- (void)stopCapture {
  dispatch_async(_queue, ^{
    if (_stopping) {
      return;
    }
    _stopping = YES;
    dispatch_semaphore_t finished = _finished;
    [_stream stopCaptureWithCompletionHandler:^(NSError *error) {
      dispatch_async(_queue, ^{ dispatch_semaphore_signal(finished); });
    }];
  });
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
  NSLog(@"Sunshine ScreenCaptureKit stream stopped: %@", error.localizedDescription);
  // Serialize terminal completion behind any in-flight pixel-buffer consumer.
  dispatch_async(_queue, ^{
    if (stream != _stream || _stopping) {
      return;
    }
    self.captureFailed = YES;
    _stopping = YES;
    dispatch_semaphore_signal(_finished);
  });
}

- (void)dealloc {
  [_callback release];
  [_stream release];
  [_display release];
  if (_finished) {
    dispatch_release(_finished);
  }
  if (_queue) {
    dispatch_release(_queue);
  }
  [super dealloc];
}

#ifdef SUNSHINE_TESTS
- (instancetype)initWithTestStream:(SCStream *)stream callback:(FrameCallbackBlock)callback {
  self = [super init];
  if (self) {
    _queue = dispatch_queue_create("io.arf.sunshine.capture.tests", DISPATCH_QUEUE_SERIAL);
    _finished = dispatch_semaphore_create(0);
    _stream = [stream retain];
    _callback = [callback copy];
  }
  return self;
}

- (dispatch_semaphore_t)testCompletion {
  return _finished;
}
#endif

@end
