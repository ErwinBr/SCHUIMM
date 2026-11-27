# The walker's actor script: the behaviour of every actor whose resource is walker.actor.json, in
# every room. Each walker runs these handlers as itself (self) with its own variables.

actor:
	var looks: int = 0

	func look_at():
		looks += 1
		print("Looking good, as always. (" + str(looks) + ")")
