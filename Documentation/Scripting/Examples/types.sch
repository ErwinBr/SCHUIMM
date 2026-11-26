# Variables, type inference, annotations and conversions.
func main():
	var actor_name = "Alex"         # string
	var walk_speed = 3.5            # float
	var has_key = True              # bool
	var steps: int = 12             # explicit annotation
	var travelled: float = steps     # int widens to float implicitly

	print("Welcome, " + actor_name)

	if has_key == False:
		print("We don't have the key.")
	else:
		print("We have the key!")

	# Conversions are explicit: str(), int() (truncates) and float().
	print("Distance: " + str(travelled * walk_speed))
	print("Whole steps: " + str(int(travelled / 5)))
	print("Half: " + str(steps / 2) + ", floor: " + str(steps // 5) + ", rest: " + str(steps % 5))
