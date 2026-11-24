# Two coroutines running concurrently (start both "narrator" and "clock").
# Run with: schuimm-run coroutines.schb --entry narrator --entry clock --dt 0.25
func narrator():
	print("Narrator: it is a quiet night.")
	wait(1.0)
	print("Narrator: suddenly, a knock on the door.")
	wait(1.0)
	print("Narrator: the end.")

func clock():
	for tick in range(1, 5):
		wait(0.5)
		print("Clock: tick " + str(tick))

func main():
	# A third, very short coroutine: yield() resumes at the next frame.
	print("Main: frame A")
	yield()
	print("Main: frame B")
