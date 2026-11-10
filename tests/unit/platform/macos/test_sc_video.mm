/** @file tests/unit/platform/macos/test_sc_video.mm
 * @brief Capture lifecycle regression tests using synthetic frames and a fake native stream.
 */
#ifdef __APPLE__
  // test includes
  #include "../../../tests_common.h"

  // local includes
  #import "src/platform/macos/sc_video.h"

/** @brief A stream fixture that completes stops without opening any screen or audio device. */
@interface ArfFakeCaptureStream: NSObject
/** @brief Number of stop requests made by the capture controller. */
@property (nonatomic, assign) NSUInteger stops;
/** @brief Complete a fake stream stop immediately.
 * @param completion Callback receiving a successful stop result.
 */
- (void)stopCaptureWithCompletionHandler:(void (^)(NSError *))completion;
@end

@implementation ArfFakeCaptureStream

- (void)stopCaptureWithCompletionHandler:(void (^)(NSError *))completion {
  self.stops++;
  completion(nil);
}

@end

/** @brief Fake legacy input that records cursor policy without opening a display. */
@interface ArfCursorScreenInput: NSObject
/** @brief Last cursor capture policy applied by AVVideo. */
@property (nonatomic, assign) BOOL capturesCursor;
@end
@implementation ArfCursorScreenInput

- (BOOL)isKindOfClass:(Class)type {
  return type == [AVCaptureScreenInput class] || [super isKindOfClass:type];
}

@end

/** @brief Fake legacy session supplying only in-memory capture inputs. */
@interface ArfCursorSession: NSObject
/** @brief Inputs to inspect when changing cursor policy. */
@property (nonatomic, retain) NSArray *inputs;
@end
@implementation ArfCursorSession

- (void)stopRunning {
}

- (void)dealloc {
  [_inputs release];
  [super dealloc];
}

@end

/** @brief Both native capture implementations honor the same opt-in cursor policy. */
TEST(ScreenCaptureKitLifecycleTest, CursorCaptureDefaultsOffAndCanBeRestored) {
  @autoreleasepool {
    SCVideo *capture = [[SCVideo alloc] init];
    capture.frameWidth = 1920;
    capture.frameHeight = 1200;
    capture.minFrameDuration = CMTimeMake(1, 60);
    EXPECT_FALSE(capture.streamConfiguration.showsCursor);
    capture.cursorVisible = YES;
    EXPECT_TRUE(capture.streamConfiguration.showsCursor);
    EXPECT_EQ(capture.streamConfiguration.queueDepth, 3);
    [capture release];

    AVVideo *legacy = [[AVVideo alloc] init];
    ArfCursorScreenInput *input = [[[ArfCursorScreenInput alloc] init] autorelease];
    ArfCursorSession *session = [[[ArfCursorSession alloc] init] autorelease];
    session.inputs = @[input, [[[NSObject alloc] init] autorelease]];
    legacy.session = (AVCaptureSession *) session;
    EXPECT_FALSE(legacy.cursorVisible);
    legacy.cursorVisible = YES;
    EXPECT_TRUE(input.capturesCursor);
    legacy.cursorVisible = NO;
    EXPECT_FALSE(input.capturesCursor);
    [legacy release];
  }
}

namespace {
  /** @brief Construct a valid in-memory image sample with a configurable capture status.
   * @param status Native frame status to attach to the sample.
   * @return Retained synthetic sample, or null on allocation failure.
   */
  CMSampleBufferRef sample(SCFrameStatus status) {
    CVPixelBufferRef pixels = nullptr;
    if (CVPixelBufferCreate(kCFAllocatorDefault, 8, 8, kCVPixelFormatType_32BGRA, nullptr, &pixels) != kCVReturnSuccess) {
      return nullptr;
    }
    CMVideoFormatDescriptionRef format = nullptr;
    CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, pixels, &format);
    CMSampleTimingInfo timing {CMTimeMake(1, 60), CMTimeMake(1, 1), kCMTimeInvalid};
    CMSampleBufferRef result = nullptr;
    CMSampleBufferCreateReadyWithImageBuffer(kCFAllocatorDefault, pixels, format, &timing, &result);
    if (format) {
      CFRelease(format);
    }
    CFRelease(pixels);
    if (result) {
      CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(result, true);
      auto metadata = (CFMutableDictionaryRef) CFArrayGetValueAtIndex(attachments, 0);
      CFDictionarySetValue(metadata, (CFStringRef) SCStreamFrameInfoStatus, (CFNumberRef) @(status));
    }
    return result;
  }
}  // namespace

TEST(ScreenCaptureKitLifecycleTest, DeliversOnlyCompleteScreenFramesAndStopsAfterRejection) {
  @autoreleasepool {
    ArfFakeCaptureStream *stream = [[[ArfFakeCaptureStream alloc] init] autorelease];
    __block int delivered = 0;
    SCVideo *capture = [[SCVideo alloc] initWithTestStream:(SCStream *) stream callback:^bool(CMSampleBufferRef) { delivered++; return false; }];
    CMSampleBufferRef idle = sample(SCFrameStatusIdle), complete = sample(SCFrameStatusComplete);
    ASSERT_NE(idle, nullptr);
    ASSERT_NE(complete, nullptr);
    [capture stream:(SCStream *) stream didOutputSampleBuffer:nullptr ofType:SCStreamOutputTypeScreen];
    [capture stream:(SCStream *) stream didOutputSampleBuffer:idle ofType:SCStreamOutputTypeScreen];
    EXPECT_EQ(delivered, 0);
    [capture stream:(SCStream *) stream didOutputSampleBuffer:complete ofType:SCStreamOutputTypeScreen];
    EXPECT_EQ(dispatch_semaphore_wait(capture.testCompletion, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)), 0);
    EXPECT_EQ(delivered, 1);
    EXPECT_EQ(stream.stops, 1U);
    EXPECT_FALSE(capture.captureFailed);
    CFRelease(idle);
    CFRelease(complete);
    [capture release];
  }
}

TEST(ScreenCaptureKitLifecycleTest, StopsAnIdleStreamOnceWithoutWaitingForAFrame) {
  @autoreleasepool {
    ArfFakeCaptureStream *stream = [[[ArfFakeCaptureStream alloc] init] autorelease];
    SCVideo *capture = [[SCVideo alloc] initWithTestStream:(SCStream *) stream callback:^bool(CMSampleBufferRef) { return true; }];
    [capture stopCapture];
    [capture stopCapture];
    EXPECT_EQ(dispatch_semaphore_wait(capture.testCompletion, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)), 0);
    EXPECT_EQ(stream.stops, 1U);
    [capture release];
  }
}

TEST(ScreenCaptureKitLifecycleTest, IgnoresRetiredStreamErrorsAndSignalsActiveFailure) {
  @autoreleasepool {
    ArfFakeCaptureStream *active = [[[ArfFakeCaptureStream alloc] init] autorelease];
    ArfFakeCaptureStream *retired = [[[ArfFakeCaptureStream alloc] init] autorelease];
    SCVideo *capture = [[SCVideo alloc] initWithTestStream:(SCStream *) active callback:^bool(CMSampleBufferRef) { return true; }];
    NSError *error = [NSError errorWithDomain:@"io.arf.capture.fixture" code:1 userInfo:nil];
    [capture stream:(SCStream *) retired didStopWithError:error];
    EXPECT_NE(dispatch_semaphore_wait(capture.testCompletion, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC)), 0);
    EXPECT_FALSE(capture.captureFailed);
    [capture stream:(SCStream *) active didStopWithError:error];
    EXPECT_EQ(dispatch_semaphore_wait(capture.testCompletion, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)), 0);
    EXPECT_TRUE(capture.captureFailed);
    [capture release];
  }
}
#endif
