#include "Adventure/AdventureInterface.h"

#include <utility>

namespace schuimm::adventure {

const script::NativeInterface& adventureInterface()
{
    static const script::NativeInterface interface = [] {
        using script::NativeCategory;
        using script::NativeType;
        using enum script::ScriptType;
        script::NativeInterface result;
        result.name = "adventure";
        result.enums.push_back(
            {"Direction", {"NORTH", "NORTH_EAST", "EAST", "SOUTH_EAST", "SOUTH", "SOUTH_WEST", "WEST", "NORTH_WEST"}});
        const NativeType direction(Enum, "Direction");
        const auto add = [&](std::string name, std::vector<NativeType> parameters, NativeType returnType,
                             NativeCategory category, std::string documentation) {
            result.functions.push_back({std::move(name), std::move(parameters), std::move(returnType), category, false,
                                        std::move(documentation), false});
        };
        const NativeCategory immediate = NativeCategory::Immediate;
        // Lookup.
        add("get_actor", {String}, Actor, immediate, "The actor with this id in the active room; a null reference if none.");
        add("get_object", {String}, Object, immediate, "The room object with this id in the active room; a null reference if none.");
        add("is_valid", {Entity}, Bool, immediate, "Whether the reference names a live actor or object of the active room.");
        // Position and visibility.
        add("get_position", {Entity}, Vec2, immediate, "Room coordinates: an actor's ground point, an object's anchor.");
        add("set_position", {Entity, Vec2}, None, immediate, "Places the entity (an actor stops walking).");
        add("is_visible", {Entity}, Bool, immediate, "Whether the entity is drawn.");
        add("set_visible", {Entity, Bool}, None, immediate, "Shows or hides the entity; nothing else changes.");
        // Direction.
        add("get_direction", {Actor}, direction, immediate, "The direction the actor faces.");
        add("set_direction", {Actor, direction}, None, immediate,
            "Turns the actor; its animation must have a clip for the direction (a walk stops).");
        // Animation.
        add("get_animation", {Entity}, String, immediate, "The current logical animation.");
        add("play_animation", {Entity, String}, Operation, NativeCategory::Background,
            "Plays a logical animation and returns its playback, which wait() can wait for (not a looping one).");
        add("stop_animation", {Entity}, None, immediate, "Stops the playback (waiters of it are cancelled).");
        add("pause_animation", {Entity}, None, immediate, "Pauses the playback; walking goes on.");
        add("resume_animation", {Entity}, None, immediate, "Resumes a paused playback.");
        // Movement.
        add("walk_to", {Actor, Vec2}, None, NativeCategory::Suspending,
            "Walks to the nearest reachable point of the destination and waits until the actor arrives.");
        add("stop_walking", {Actor}, None, immediate, "Stops the actor where it is (a coroutine waiting for the walk is cancelled).");
        add("is_walking", {Actor}, Bool, immediate, "Whether the actor is walking.");
        // Scale and speed.
        add("get_scale", {Actor}, Float, immediate, "The actor's base scale.");
        add("set_scale", {Actor, Float}, None, immediate, "Sets the base scale (0.001 to 1000).");
        add("get_effective_scale", {Actor}, Float, immediate, "Base scale times the room's perspective scale.");
        add("get_walk_speed", {Actor}, Float, immediate, "Walking speed in room pixels per second at perspective scale 1.");
        add("set_walk_speed", {Actor, Float}, None, immediate, "Sets the walking speed (also of a walk in progress).");
        // Camera.
        add("get_camera_position", {}, Vec2, immediate, "The centre of the camera view, in room coordinates.");
        add("set_camera_position", {Vec2}, None, immediate, "Moves the camera at once (kept inside its bounds).");
        add("get_camera_zoom", {}, Float, immediate, "The camera zoom.");
        add("set_camera_zoom", {Float}, None, immediate, "Sets the zoom at once (clamped to the camera's limits).");
        return result;
    }();
    return interface;
}

} // namespace schuimm::adventure
