# The player's verb (ActorDemo: C cycles it, T uses it on the selected actor). The enum is
# game-wide: the actor scripts compare with Verb.LOOK and Verb.TALK without declaring it.

enum Verb:
	LOOK
	USE
	TALK

var selected_verb: Verb = Verb.LOOK


func cycle_verb():
	if selected_verb == Verb.LOOK:
		selected_verb = Verb.USE
	elif selected_verb == Verb.USE:
		selected_verb = Verb.TALK
	else:
		selected_verb = Verb.LOOK
	print("verb: " + verb_name(selected_verb))


func verb_name(verb: Verb) -> string:
	if verb == Verb.LOOK:
		return "look at"
	if verb == Verb.USE:
		return "use"
	return "talk to"
