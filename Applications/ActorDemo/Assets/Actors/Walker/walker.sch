# The walker's own script. Actor scripts are associated with actor resources but not executed
# yet: event handlers for actors arrive with the adventure scripting milestone. This file is
# compiled with the demo so the reference in walker.actor.json points at real bytecode.

func look_at():
	print("Looking good, as always.")
