# Game-wide state and functions of the ActorDemo game. Every script of the game uses them by
# name: there are no imports. A global variable exists once for the whole game session and keeps
# its value while rooms come and go.

var score: int = 0
var rooms_entered: int = 0


# Awards points after a short pause: a global function that suspends, and its caller with it.
func award_points(amount: int, reason: string):
	wait(0.5)
	score += amount
	print("score: " + str(score) + " (" + reason + ")")


# Started once by ActorDemo as a coroutine of the game session: it survives every room change.
func session_clock():
	var seconds = 0
	while True:
		wait(15.0)
		seconds += 15
		print("session: " + str(seconds) + " s played, score " + str(score) + ", " + str(rooms_entered) + " rooms entered")
