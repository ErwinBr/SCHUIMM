#include "Room/ActorLibrary.h"

#include "Animation/AnimationResourceLoader.h"
#include "Resource/Image/ImageLoader.h"
#include "Room/ActorResourceLoader.h"

namespace schuimm::room {

ActorLibrary::ActorLibrary(const AssetLocator& locator)
    : images_(locator, loadImageInfo)
    , animations_(locator,
                  [this](const AssetLocator& l, const AssetPath& asset) {
                      return animation::loadAnimationResource(l, asset, images_);
                  })
    , actors_(locator, [this](const AssetLocator& l, const AssetPath& asset) {
        const AnimationResolver resolver = [this](const AssetPath& resource) { return loadAnimation(resource); };
        return loadActorResource(l, asset, resolver);
    })
{
}

Result<std::shared_ptr<const ActorResource>> ActorLibrary::load(const AssetPath& actor)
{
    return actors_.load(ResourceId<ActorResource>(actor));
}

Result<std::shared_ptr<const animation::AnimationResource>> ActorLibrary::loadAnimation(const AssetPath& resource)
{
    return animations_.load(ResourceId<animation::AnimationResource>(resource));
}

void ActorLibrary::unloadAll()
{
    actors_.unloadAll();
    animations_.unloadAll();
    images_.unloadAll();
}

} // namespace schuimm::room
