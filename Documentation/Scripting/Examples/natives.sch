# Engine functions are declared with "extern func" and linked by name and signature when
# the bytecode is loaded. This example needs a host that registers play_sound().
extern func play_sound(name: string, volume: float)

func main():
	play_sound("chime", 0.8)
	wait(0.5)
	play_sound("step", 1)    # int arguments widen to float
