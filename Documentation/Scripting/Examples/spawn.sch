# spawn starts a function as an independent coroutine; the caller continues at once.
# Run with: schuimm-run spawn.schb --dt 0.5
func blink(name: string, times: int):
	for i in range(times):
		print(name + " blinks")
		wait(1.0)

func ticker():
	var count = 0
	while True:
		count = count + 1
		print("tick " + str(count))
		wait(0.5)

func main():
	spawn blink("lamp", 2)
	var clock: coroutine = spawn ticker()
	print("main: both started")
	wait(1.25)
	cancel(clock)       # Stops the ticker; cancelling it again would be harmless.
	print("main: ticker cancelled")
	spawn wait(0.5)     # Natives can be spawned too.
	print("main: done")  # The lamp keeps blinking after main has returned.
