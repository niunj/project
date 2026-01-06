// Copyright (c) 2006-2018 Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Virga.h
    \brief Renders rain hanging underneath a cloud.
 */

#ifndef VIRGA_H
#define VIRGA_H

#include "Atmosphere.h"
#include "Renderable.h"
#include "Renderer.h"

namespace SilverLining
{
class Cloud;
class Billboard;

/** Renders rain hanging underneath a cloud, which may or may not actually reach the ground. This
   is done using a billboarding effect.*/
class Virga : public Renderable
{
public:
/** Constructs a Virga object of a given size and position, attached to a given Cloud.
   \param parentCloud The Cloud to associate this Virga with.
   \param diameter The diameter of the area of rain underneath the cloud.
   \param height The distance from the base of the cloud, plus the offsetPos provided, to the
   bottom of the Virga. For rain, this must be greater than the distance to the ground.
   \param offsetPos An offset from the center of the cloud from which to start drawing the virga
   from.
   \param atm The Atmosphere object used to render the Cloud.
 */
    Virga(Cloud *parentCloud, double diameter, double height, const Vector3& offsetPos,
          const Atmosphere& atm, ThreadCameraStreamData* tcsData);

/** Virtual destructor; releases any resources allocated internally. */
    virtual ~Virga();

/** Sets the color of the virga, which modulates the virga texture map. */
    void SetColor(const Color& c, const Vector3& camPos, ThreadCameraStreamData* tcsData);

/** Draws the virga, or rather schedules it for drawing once all translucent objects in the
   scene have been sorted via DrawBlendedObject().
   \param pass Set to 0 for the lighting pass, or 1 for the rendering pass.
   \return True if the operation succeeded.
 */
    virtual bool Draw(int pass, ThreadCameraStreamData* tcsData);

/** Returns the position of the virga in world coordinates. */
    virtual Vector3 GetWorldPosition(const ThreadCameraStreamData* tcsData) const;

/** Does the actual drawing of the Virga, once the framework has sorted it against all other
   translucent objects in the scene. */
    virtual void DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const;

protected:
    void GetDrift(Matrix4& driftMatrix, unsigned long millis) const;

    Vector3 offsetPos;
    
    Cloud *cloud;
    double yScale;
    Billboard *billboard;
    const Atmosphere& atmosphere;
    float virgaAlpha;
    float driftOffset, driftSpeed, driftRadius;
    float fadeDistance;
    double diameter;

    ThreadCameraStreamData* tcsData;

    TextureHandle virgaTexture = 0;
};
}

#endif
