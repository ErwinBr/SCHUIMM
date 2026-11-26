# The courtyard's room script. Compiled with the adventure interface (schuimmc --interface
# adventure, done by the build), so get_actor, walk_to, Direction and the rest need no
# declarations.
#
# ActorDemo starts enter() when the room is entered, starts demonstrate() on P, and dispatches
# fan.use on F. Everything here runs in coroutines owned by the room: leaving the room cancels it.


# The gardener's rounds. walk_to waits until the gardener arrives; wait() pauses the round.
func patrol(gardener: actor):
	while True:
		walk_to(gardener, vec2(820, 300))
		set_direction(gardener, Direction.SOUTH)
		wait(1.5)
		walk_to(gardener, vec2(120, 260))
		walk_to(gardener, vec2(140, 500))
		set_direction(gardener, Direction.EAST)
		wait(1.0)
		walk_to(gardener, vec2(880, 490))


func enter():
	print("courtyard: the gardener starts its rounds")
	spawn patrol(get_actor("gardener"))


# A point beside `target`, `gap` pixels to its left.
func left_of(target: entity, gap: float) -> vec2:
	var point = get_position(target)
	point.x = point.x - gap
	return point


func demonstrate():
	var hero: actor = get_actor("hero")
	var gardener: actor = get_actor("gardener")
	var fan: object = get_object("fan")
	print("demonstrate: start")

	# A second patrol for the gardener, in its own coroutine. Its first walk replaces the room's
	# patrol walk: that coroutine is cancelled and does not continue its sequence.
	var patrol_task: coroutine = spawn patrol(gardener)

	# The balcony cannot be reached from the floor: the hero walks to the nearest reachable point.
	walk_to(hero, vec2(800, 150))
	print("demonstrate: nearest reachable point " + str(int(get_position(hero).x)) + ", " + str(int(get_position(hero).y)))

	# Vectors: a point beside the fan, computed from its position.
	var stand = left_of(fan, 36) + vec2(0, 24)
	walk_to(hero, stand)
	set_direction(hero, Direction.EAST)

	# Wait for a non-looping animation, then start a looping one that runs on its own.
	wait(play_animation(hero, "press_button"))
	play_animation(fan, "spinning")
	print("demonstrate: the fan spins (" + get_animation(fan) + ")")

	# A native spawned directly: the hero walks off while this coroutine zooms the camera.
	spawn walk_to(hero, vec2(320, 480))
	var zoom = get_camera_zoom()
	set_camera_zoom(zoom * 1.25)
	print(get_camera_position())
	wait(1.5)
	set_camera_zoom(zoom)
	while is_walking(hero):
		yield()

	# Cancel the second patrol: the gardener stops where it is.
	cancel(patrol_task)
	set_position(gardener, get_position(hero) + vec2(70, 0))
	set_direction(gardener, Direction.WEST)
	print("demonstrate: the gardener came over, facing " + str(get_direction(gardener) == Direction.WEST))
	wait(2.0)
	spawn patrol(gardener)
	print("demonstrate: done")


object fan:
	func use():
		if get_animation(self) == "spinning":
			play_animation(self, "idle")
			print("fan: off")
		else:
			play_animation(self, "spinning")
			print("fan: on")
