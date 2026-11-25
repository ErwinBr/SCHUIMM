# A room script: the kitchen's room-level functions and the handlers of its objects.
#
# The room resource defines the objects "blender" and "cupboard" (sprites, positions, state).
# This script only says what happens when the player interacts with them: the engine invokes
# a handler by object identifier and event name ("blender", "use") and passes the object as
# self. Objects without a declaration here, or events without a handler, simply do nothing.
#
# Object state lives in the engine; scripts reach it through natives that take an object.

extern func say(text: string)
extern func get_flag(o: object, name: string) -> bool
extern func set_flag(o: object, name: string, value: bool)
extern func inventory_item(name: string) -> object
extern func play_sound(name: string, volume: float)


func enter():
	print("Welcome to the kitchen.")


func play_blender_sound():
	play_sound("blender.wav", 1.0)


object blender:
	func look_at():
		say("An old blender.")

	func use():
		if get_flag(self, "is_running"):
			say("It's already running.")
		else:
			set_flag(self, "is_running", True)
			play_blender_sound()
			wait(2.0)
			set_flag(self, "is_running", False)
			say("The blender stops.")

	func use_with(item: object):
		if item == inventory_item("banana"):
			say("A banana smoothie sounds good.")
		else:
			say("That won't work.")


object cupboard:
	func look_at():
		say("A wooden cupboard.")
