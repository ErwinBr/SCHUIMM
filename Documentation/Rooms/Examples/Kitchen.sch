# The room script of Kitchen.room.json, compiled at build time with
#   schuimmc Kitchen.sch -o Kitchen.schb
# and referenced by the room resource as "Rooms/Kitchen/Kitchen.schb".
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
