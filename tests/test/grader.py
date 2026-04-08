import os
import subprocess
import sys

import checker as checker


def get_grade(executable: str, test_cases_folder: str) -> float:
    grade = 0.0
    number_of_test_cases = 0

    os.makedirs("my-outputs", exist_ok=True)
    subprocess.run(["make"], check=True)

    for input_file in sorted(os.listdir(test_cases_folder)):
        if not input_file.startswith("input"):
            continue

        output_file = input_file.replace("input", "output", 1)
        number_of_test_cases += 1

        try:
            test_grade = checker.run(
                executable,
                f"{test_cases_folder}/{input_file}",
                f"my-outputs/{output_file}",
                f"{test_cases_folder}/{output_file}",
            )
        except Exception as error:
            print(error, file=sys.stderr)
            test_grade = 0.0

        grade += test_grade
        print(
            f"\033[93mtest-case '{input_file}': {test_grade * 100:.2f} points\033[0m",
            file=sys.stderr,
        )

    if number_of_test_cases == 0:
        raise RuntimeError(f"No input test cases found in '{test_cases_folder}'")

    return grade * 100 / number_of_test_cases


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python3 grader.py <executable> <test_cases_folder>")
        sys.exit(1)

    grade = get_grade(sys.argv[1], sys.argv[2])

    print(f"\033[92mGrade = {grade:.2f}\033[0m", file=sys.stderr)
    print(f"{grade:.2f}")
