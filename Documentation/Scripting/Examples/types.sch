# Variables, type inference, annotations and conversions.
func main():
	actor_name = "Alex"         # string
	walk_speed = 3.5            # float
	has_key = True              # bool
	steps: int = 12             # explicit annotation
	distance: float = steps     # int widens to float implicitly

	print("Welcome, " + actor_name)

	if has_key == False:
		print("We don't have the key.")
	else:
		print("We have the key!")

	# Conversions are explicit: str(), int() (truncates) and float().
	print("Distance: " + str(distance * walk_speed))
	print("Whole steps: " + str(int(distance / 5)))
	print("Half: " + str(steps / 2) + ", floor: " + str(steps // 5) + ", rest: " + str(steps % 5))
