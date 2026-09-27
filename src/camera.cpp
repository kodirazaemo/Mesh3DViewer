#include "camera.hpp"

#include <algorithm>
#include <cmath>

namespace meshviewer {

void OrbitCamera::AddOrbit(float deltaXPixels, float deltaYPixels) {
  constexpr float kRadiansPerPixel = 0.005f;
  constexpr float kPitchLimit = 1.55334306f;
  yaw_ += deltaXPixels * kRadiansPerPixel;
  pitch_ -= deltaYPixels * kRadiansPerPixel;
  pitch_ = std::clamp(pitch_, -kPitchLimit, kPitchLimit);
}

void OrbitCamera::AddZoom(float wheelDelta) {
  constexpr float kWheelDelta = 120.0f;
  distance_ *= std::pow(0.9f, wheelDelta / kWheelDelta);
  // The framed mesh is about 2 units across, so stay outside that bounding sphere.
  distance_ = std::clamp(distance_, 1.4f, 40.0f);
}

void OrbitCamera::SetAspect(float aspect) {
  if (aspect > 0.0f) {
    aspect_ = aspect;
  }
}

DirectX::XMMATRIX OrbitCamera::View() const {
  using namespace DirectX;
  const float cosPitch = std::cos(pitch_);
  const XMVECTOR eye = XMVectorSet(distance_ * cosPitch * std::sin(yaw_), distance_ * std::sin(pitch_),
                                   distance_ * cosPitch * std::cos(yaw_), 1.0f);
  return XMMatrixLookAtRH(eye, XMVectorZero(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

DirectX::XMMATRIX OrbitCamera::Projection() const {
  using namespace DirectX;
  return XMMatrixPerspectiveFovRH(XMConvertToRadians(45.0f), aspect_, 0.05f, 100.0f);
}

}  // namespace meshviewer
