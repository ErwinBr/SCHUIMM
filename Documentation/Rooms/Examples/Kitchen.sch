# The room script of Kitchen.room.json, which refers to it as "Rooms/Kitchen/Kitchen.sch". The
# build compiles it with the game's other scripts into the game's program (schuimmc --game); on
# its own it can be checked with
#   schuimmc Kitchen.sch
#
# Each "object NAME:" names an object of the room resource ("blender", "cupboard"). The room's
# third object, "window", has no declaration: it is valid and simply has no handlers.

extern func say(text: string)


func enter():
	print("Entering the kitchen")


object blender:
	func look_at():
		say("An old blender.")


object cupboard:
	func look_at():
		say("A wooden cupboard.")
