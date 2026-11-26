#include "PlatformAnimationEffects.h"
#include "EngagementVisualFeature.h"

namespace vsgraysim::viewport3d {

void publishAnimationEffects(EngagementVisualFeature& renderer, std::string_view owner,
                             const platform_animation::AnimationFrame& frame,
                             const AnimationAttachments& attachment) {
    using Style = platform_animation::EffectStyle;
    using Particle = AviationParticles::Style;
    std::vector<EngagementVisualFeature::Plume> plumes;
    for (const auto& effect : frame.effects) {
        const auto matrix = attachment(effect.attachment);
        if (!matrix) continue;
        const auto& p = effect.offset;
        const auto& d = effect.direction;
        const vsg::vec3 position(*matrix * vsg::dvec3(p[0],p[1],p[2]));
        const auto transformed = *matrix * vsg::dvec4(d[0],d[1],d[2],0.);
        const vsg::vec3 direction(float(transformed.x),float(transformed.y),float(transformed.z));
        if (effect.style == Style::Flash) {
            renderer.addPreviewFlash(position,direction,effect.radius);
            continue;
        }
        const auto style = effect.style==Style::Jet ? Particle::Jet :
                           effect.style==Style::LaunchExhaust ? Particle::LaunchExhaust : Particle::WarmExhaust;
        plumes.push_back({std::string(owner)+":"+std::string(effect.id),position,direction,0,style==Particle::Jet,
                          effect.strength,effect.radius,style,effect.exhaust});
    }
    renderer.setPlatformPlumes(std::string(owner),std::move(plumes));
}

} // namespace vsgraysim::viewport3d
