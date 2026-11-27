# The walker's actor script: the behaviour of every actor whose resource is walker.actor.json, in
# every room (the hall's hero and guards, the garden's hero and gardener). Each walker runs these
# handlers as itself (self), with its own variables.

# A global variable, declared here and shared by all walkers and every other script.
var walker_conversations: int = 0

actor:
	var looks: int = 0
	var conversations: int = 0

	# ActorDemo's T key: the selected verb (Scripts/Interface.sch) decides what happens.
	func interact():
		if selected_verb == Verb.LOOK:
			look_at()
		elif selected_verb == Verb.TALK:
			talk_to()
		else:
			print("You cannot use a person.")

	func look_at():
		looks += 1
		print("Looking good, as always. (looked at " + str(looks) + " times)")

	func talk_to():
		conversations += 1
		walker_conversations += 1
		print("walker: conversation " + str(conversations) + " with me, " + str(walker_conversations) + " with walkers")
		if fan_repaired:
			print("walker: thanks for fixing the courtyard fan!")
		spawn wave()    # runs on its own, for this walker; ends when the room is left

	func wave():
		wait(play_animation(self, "wave"))
		play_animation(self, "idle")
		award_points(1, "a friendly wave")
