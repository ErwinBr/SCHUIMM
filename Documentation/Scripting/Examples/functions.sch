# Functions, parameters, return values, recursion and for loops.
func calculate_damage(amount: int, multiplier: float) -> float:
	return amount * multiplier

func fibonacci(n: int) -> int:
	if n < 2:
		return n
	return fibonacci(n - 1) + fibonacci(n - 2)

func describe(health: int) -> string:
	if health <= 0:
		return "defeated"
	elif health < 50:
		return "wounded"
	return "healthy"

func main():
	print(calculate_damage(10, 1.5))
	for i in range(0, 10, 3):
		print("fib(" + str(i) + ") = " + str(fibonacci(i)))
	for health in range(100, -1, -60):
		print(str(health) + ": " + describe(health))
