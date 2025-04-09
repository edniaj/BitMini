# Get input for number of grades
a = int(input('how many A: '))
a_minus = int(input("how many A-: "))
b_plus = int(input("how many B+: "))
b = int(input("how many B: "))

# Calculate total grade points
total_points = (a_minus * 4.5) + (a * 5.0) + (b_plus * 4.0) + (b * 3.5)

# Calculate total number of courses
total_courses = a_minus + a + b_plus + b

# Calculate current CGPA
if total_courses > 0:
    cgpa = total_points / total_courses
    print(f"Your current CGPA is: {cgpa:.2f}")
else:
    print("No courses entered")
    exit()

print("\nProjected CGPA scenarios for 7 future modules (only showing combinations with CGPA > 4.0):")

# Generate all possible combinations of A-, B+, and B for 7 modules
combinations_found = False
for a_minus_count in range(8):  # 0 to 7 A-
    for b_plus_count in range(8 - a_minus_count):  # Remaining modules for B+
        b_count = 7 - a_minus_count - b_plus_count  # Remaining modules for B
        
        # Calculate new total points and courses
        new_total_points = total_points + (a_minus_count * 4.5) + (b_plus_count * 4.0) + (b_count * 3.5)
        new_total_courses = total_courses + 7
        projected_cgpa = new_total_points / new_total_courses
        
        # Only print combinations with CGPA > 4.0
        if projected_cgpa > 4.0:
            combinations_found = True
            print(f"With {a_minus_count}A-, {b_plus_count}B+, {b_count}B: {projected_cgpa:.2f}")

if not combinations_found:
    print("No combinations found with CGPA > 4.0")

print("\nSummary of best and worst scenarios:")
# Find best and worst scenarios
best_cgpa = 0
worst_cgpa = 5.0
best_combination = ""
worst_combination = ""

for a_minus_count in range(8):
    for b_plus_count in range(8 - a_minus_count):
        b_count = 7 - a_minus_count - b_plus_count
        
        new_total_points = total_points + (a_minus_count * 4.5) + (b_plus_count * 4.0) + (b_count * 3.5)
        new_total_courses = total_courses + 7
        projected_cgpa = new_total_points / new_total_courses
        
        if projected_cgpa > best_cgpa:
            best_cgpa = projected_cgpa
            best_combination = f"{a_minus_count}A-, {b_plus_count}B+, {b_count}B"
        
        if projected_cgpa < worst_cgpa:
            worst_cgpa = projected_cgpa
            worst_combination = f"{a_minus_count}A-, {b_plus_count}B+, {b_count}B"

print(f"Best scenario: {best_combination} → {best_cgpa:.2f}")
print(f"Worst scenario: {worst_combination} → {worst_cgpa:.2f}")