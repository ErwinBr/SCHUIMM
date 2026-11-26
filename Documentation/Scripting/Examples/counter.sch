# The Milestone 3 demonstration script: a loop that waits without blocking the game.
func main():
	print("Welcome to SCHUIMM!")

	var counter = 0

	while counter < 3:
		print(counter)
		counter = counter + 1
		wait(1.0)

	print("Finished!")
