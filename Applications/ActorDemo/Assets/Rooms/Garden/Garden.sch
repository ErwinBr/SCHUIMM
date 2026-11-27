# The garden's room script. Its room variables start afresh every time the garden is entered; the
# globals it reads (courtyard_visits, fan_repaired) are declared in the courtyard's script.

room:
	var visits_here: int = 0

	# Prepares the scene before anything happens in it: every actor already exists.
	func init():
		set_visible(get_actor("lamp"), fan_repaired)

	func ready():
		visits_here += 1
		rooms_entered += 1
		print("garden: visit " + str(visits_here) + " of this garden, courtyard visits so far: " + str(courtyard_visits))
		describe_fountain()    # a global function of the courtyard's script

	func exit():
		print("garden: goodbye")
