import AVFoundation
import ImagoCore
import VideoToolbox

/// Hardware decoding and display of the payload's video.
///
/// The core hands each compressed frame over on its video thread; it is decoded right there,
/// synchronously, by VideoToolbox in real-time mode, and the picture goes to an
/// AVSampleBufferDisplayLayer marked "display immediately". There is no queue in between: if the
/// layer is still busy with the previous picture, the new one replaces it rather than waiting.
final class VideoPipeline: VideoFrameSink {
    let displayLayer = AVSampleBufferDisplayLayer()

    /// Called on the main queue when the picture size changes (for overlays and tap mapping).
    var onVideoSizeChange: ((CGSize) -> Void)?

    private let reporterLock = NSLock()
    private var timingReporter: ((Int64, Int64, Int64) -> Void)?

    // Video thread state. reset() touches it only after the session is stopped.
    private var codec = IMAGO_CODEC_UNKNOWN
    private var formatDescription: CMVideoFormatDescription?
    private var decoder: VTDecompressionSession?
    private var displayFormat: CMVideoFormatDescription?
    private var pictureSize = CGSize.zero

    init() {
        displayLayer.videoGravity = .resizeAspect
        displayLayer.backgroundColor = CGColor(gray: 0, alpha: 1)
    }

    /// receive, decoded, presented (monotonic ns) for the HUD's latency figures.
    func setTimingReporter(_ reporter: ((Int64, Int64, Int64) -> Void)?) {
        reporterLock.lock()
        timingReporter = reporter
        reporterLock.unlock()
    }

    /// Drops the decoder and clears the screen. Call only while no session is running.
    func reset() {
        if let decoder {
            VTDecompressionSessionInvalidate(decoder)
        }
        decoder = nil
        formatDescription = nil
        displayFormat = nil
        codec = IMAGO_CODEC_UNKNOWN
        displayLayer.sampleBufferRenderer.flush(removingDisplayedImage: true, completionHandler: nil)
    }

    // MARK: VideoFrameSink (core video thread)

    func decode(_ framePointer: UnsafePointer<imago_video_frame>) {
        let frame = framePointer.pointee
        if decoder == nil || frame.parameter_sets_changed || frame.codec != codec {
            guard rebuildDecoder(for: frame) else { return }
        }
        guard let decoder, let formatDescription, let data = frame.data, frame.size > 0 else {
            return
        }

        // The frame's bytes are only valid during this call, so they are copied once into a
        // block buffer that VideoToolbox owns.
        var block: CMBlockBuffer?
        guard CMBlockBufferCreateWithMemoryBlock(
            allocator: kCFAllocatorDefault, memoryBlock: nil, blockLength: frame.size,
            blockAllocator: kCFAllocatorDefault, customBlockSource: nil, offsetToData: 0,
            dataLength: frame.size, flags: kCMBlockBufferAssureMemoryNowFlag,
            blockBufferOut: &block) == noErr,
            let block,
            CMBlockBufferReplaceDataBytes(with: data, blockBuffer: block, offsetIntoDestination: 0,
                                          dataLength: frame.size) == noErr
        else { return }

        var sample: CMSampleBuffer?
        var sampleSize = frame.size
        guard CMSampleBufferCreateReady(
            allocator: kCFAllocatorDefault, dataBuffer: block, formatDescription: formatDescription,
            sampleCount: 1, sampleTimingEntryCount: 0, sampleTimingArray: nil,
            sampleSizeEntryCount: 1, sampleSizeArray: &sampleSize, sampleBufferOut: &sample) == noErr,
            let sample
        else { return }

        let receiveNs = frame.receive_time_ns
        var infoFlags = VTDecodeInfoFlags()
        let status = VTDecompressionSessionDecodeFrame(
            decoder, sampleBuffer: sample, flags: [._1xRealTimePlayback],
            infoFlagsOut: &infoFlags
        ) { [weak self] status, _, imageBuffer, _, _ in
            guard status == noErr, let imageBuffer else { return }
            self?.display(imageBuffer, receiveNs: receiveNs)
        }
        if status == kVTInvalidSessionErr {
            // Happens after the app returns from the background: rebuild on the next frame.
            VTDecompressionSessionInvalidate(decoder)
            self.decoder = nil
        }
    }

    // MARK: Decoder setup

    private func rebuildDecoder(for frame: imago_video_frame) -> Bool {
        if let decoder {
            VTDecompressionSessionInvalidate(decoder)
            self.decoder = nil
        }
        guard let format = Self.makeFormatDescription(frame) else { return false }

        var session: VTDecompressionSession?
        // No pixel format requested: the decoder's native output avoids a conversion pass.
        // IOSurface backing is what the display layer needs; the hardware decoder gives it anyway,
        // the software one (iOS Simulator) does not unless asked, and the layer then shows nothing.
        let attributes = [kCVPixelBufferIOSurfacePropertiesKey: [String: Any]()] as CFDictionary
        let status = VTDecompressionSessionCreate(
            allocator: kCFAllocatorDefault, formatDescription: format, decoderSpecification: nil,
            imageBufferAttributes: attributes, outputCallback: nil, decompressionSessionOut: &session)
        guard status == noErr, let session else { return false }
        VTSessionSetProperty(session, key: kVTDecompressionPropertyKey_RealTime,
                             value: kCFBooleanTrue)

        decoder = session
        formatDescription = format
        codec = frame.codec

        let dimensions = CMVideoFormatDescriptionGetDimensions(format)
        let size = CGSize(width: CGFloat(dimensions.width), height: CGFloat(dimensions.height))
        if size != pictureSize {
            pictureSize = size
            DispatchQueue.main.async { [weak self] in self?.onVideoSizeChange?(size) }
        }
        return true
    }

    private static func makeFormatDescription(_ frame: imago_video_frame) -> CMVideoFormatDescription? {
        guard let sps = frame.sps, let pps = frame.pps, frame.sps_size > 0, frame.pps_size > 0 else {
            return nil
        }
        var format: CMFormatDescription?
        var status: OSStatus = -1
        if frame.codec == IMAGO_CODEC_H265 {
            guard let vps = frame.vps, frame.vps_size > 0 else { return nil }
            let pointers: [UnsafePointer<UInt8>] = [vps, sps, pps]
            let sizes: [Int] = [frame.vps_size, frame.sps_size, frame.pps_size]
            status = pointers.withUnsafeBufferPointer { p in
                sizes.withUnsafeBufferPointer { s in
                    CMVideoFormatDescriptionCreateFromHEVCParameterSets(
                        allocator: kCFAllocatorDefault, parameterSetCount: 3,
                        parameterSetPointers: p.baseAddress!, parameterSetSizes: s.baseAddress!,
                        nalUnitHeaderLength: 4, extensions: nil, formatDescriptionOut: &format)
                }
            }
        } else {
            let pointers: [UnsafePointer<UInt8>] = [sps, pps]
            let sizes: [Int] = [frame.sps_size, frame.pps_size]
            status = pointers.withUnsafeBufferPointer { p in
                sizes.withUnsafeBufferPointer { s in
                    CMVideoFormatDescriptionCreateFromH264ParameterSets(
                        allocator: kCFAllocatorDefault, parameterSetCount: 2,
                        parameterSetPointers: p.baseAddress!, parameterSetSizes: s.baseAddress!,
                        nalUnitHeaderLength: 4, formatDescriptionOut: &format)
                }
            }
        }
        return status == noErr ? format : nil
    }

    // MARK: Display

    private func display(_ imageBuffer: CVImageBuffer, receiveNs: Int64) {
        let decodedNs = imago_now_ns()

        if displayFormat == nil
            || !CMVideoFormatDescriptionMatchesImageBuffer(displayFormat!, imageBuffer: imageBuffer) {
            var format: CMVideoFormatDescription?
            CMVideoFormatDescriptionCreateForImageBuffer(allocator: kCFAllocatorDefault,
                                                         imageBuffer: imageBuffer,
                                                         formatDescriptionOut: &format)
            displayFormat = format
        }
        guard let displayFormat else { return }

        var timing = CMSampleTimingInfo(duration: .invalid,
                                        presentationTimeStamp: CMClockGetTime(CMClockGetHostTimeClock()),
                                        decodeTimeStamp: .invalid)
        var sample: CMSampleBuffer?
        guard CMSampleBufferCreateReadyWithImageBuffer(
            allocator: kCFAllocatorDefault, imageBuffer: imageBuffer,
            formatDescription: displayFormat, sampleTiming: &timing, sampleBufferOut: &sample) == noErr,
            let sample
        else { return }

        if let attachments = CMSampleBufferGetSampleAttachmentsArray(sample, createIfNecessary: true),
           CFArrayGetCount(attachments) > 0 {
            let dictionary = unsafeBitCast(CFArrayGetValueAtIndex(attachments, 0),
                                           to: CFMutableDictionary.self)
            CFDictionarySetValue(dictionary,
                                 Unmanaged.passUnretained(kCMSampleAttachmentKey_DisplayImmediately).toOpaque(),
                                 Unmanaged.passUnretained(kCFBooleanTrue).toOpaque())
        }

        let renderer = displayLayer.sampleBufferRenderer
        if renderer.status == .failed || renderer.requiresFlushToResumeDecoding {
            renderer.flush()
        }
        // Never build a backlog: a picture that cannot be shown now is skipped.
        if renderer.isReadyForMoreMediaData {
            renderer.enqueue(sample)
        }

        reporterLock.lock()
        let reporter = timingReporter
        reporterLock.unlock()
        // The layer shows the picture on the next screen refresh; 0 = not measured.
        reporter?(receiveNs, decodedNs, 0)
    }
}
