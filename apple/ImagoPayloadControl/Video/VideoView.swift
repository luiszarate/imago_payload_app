import AVFoundation
import SwiftUI
import UIKit

/// Hosts the pipeline's display layer. The layer outlives the view, so it moves between hosts.
struct VideoView: UIViewRepresentable {
    let pipeline: VideoPipeline

    func makeUIView(context: Context) -> VideoHostView {
        let view = VideoHostView()
        view.host(pipeline.displayLayer)
        return view
    }

    func updateUIView(_ view: VideoHostView, context: Context) {
        view.host(pipeline.displayLayer)
    }
}

final class VideoHostView: UIView {
    private weak var videoLayer: CALayer?

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .black
        isUserInteractionEnabled = false
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not used")
    }

    func host(_ layer: CALayer) {
        guard layer.superlayer !== self.layer else { return }
        layer.removeFromSuperlayer()
        self.layer.addSublayer(layer)
        videoLayer = layer
        setNeedsLayout()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        videoLayer?.frame = bounds
        CATransaction.commit()
    }
}
