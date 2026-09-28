#pragma once

#include <DirectXMath.h>

namespace meshviewer
{

// Orbit camera aimed at the origin. Left-drag rotates, wheel changes distance.
// View and projection are right-handed so Assimp's authored winding stays CCW.
class OrbitCamera
{
  public:
    void AddOrbit(float deltaXPixels, float deltaYPixels);
    void AddZoom(float wheelDelta);
    void SetAspect(float aspect);

    [[nodiscard]] DirectX::XMMATRIX View() const;
    [[nodiscard]] DirectX::XMMATRIX Projection() const;

  private:
    float yaw_ = 0.9f;
    float pitch_ = 0.35f;
    float distance_ = 4.5f;
    float aspect_ = 16.0f / 9.0f;
};

} // namespace meshviewer
