# Used by the ScriptRunnerDispatchCli test: schuimm-run --dispatch.
func describe(name: string):
	print("You see " + name + ".")


object lamp:
	func look_at():
		describe("a lamp")
		print(self)

	func use():
		print("Click.")
		wait(0.5)
		print("The lamp is on.")
