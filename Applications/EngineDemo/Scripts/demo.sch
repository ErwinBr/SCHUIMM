# EngineDemo scripts, compiled to Assets/Scripts/demo.schb at build time by schuimmc.
# The demo starts "main" and "ticker" as two concurrent coroutines; both wait without
# blocking the game loop, which keeps rendering and handling input.

# Implemented by the demo (DemoListener): plays the chime sound effect.
extern func play_chime()

func main():
	print("Welcome to SCHUIMM!")

	var counter = 0

	while counter < 3:
		print(counter)
		counter = counter + 1
		wait(1.0)

	play_chime()
	print("Finished!")

func ticker():
	for beat in range(1, 7):
		wait(0.75)
		print("ticker: beat " + str(beat))
	print("ticker: done")
