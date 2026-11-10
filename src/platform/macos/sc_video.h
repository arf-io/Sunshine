/** @file src/platform/macos/sc_video.h
 * @brief ScreenCaptureKit implementation of the existing CVPixelBuffer capture interface.
 */
#pragma once
#import "av_video.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>

/** @brief Bounded-queue screen capture using the native display's presentation timestamps. */
API_AVAILABLE(macos(12.3))
@interface SCVideo: AVVideo <SCStreamOutput, SCStreamDelegate>
/** @brief Whether the capture backend stopped because of an error. */
@property (nonatomic, assign) BOOL captureFailed;
/** @brief Stop an idle stream and signal its capture loop after callbacks drain. */
- (void)stopCapture;
#ifdef SUNSHINE_TESTS
/** @brief Attach an isolated fake stream without requesting screen capture.
 * @param stream Fake stream implementing stopCaptureWithCompletionHandler.
 * @param callback Consumer used to test buffer filtering and termination.
 * @return Initialized test capture controller.
 */
- (instancetype)initWithTestStream:(SCStream *)stream callback:(FrameCallbackBlock)callback;
/** @brief Return the termination signal for bounded lifecycle tests.
 * @return Semaphore signaled after capture callbacks drain.
 */
- (dispatch_semaphore_t)testCompletion;
#endif
@end
