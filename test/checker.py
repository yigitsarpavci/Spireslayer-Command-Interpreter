import os
import sys
from subprocess import PIPE, Popen


class CustomError(Exception):
    def __init__(self, message: str):
        self.message = f"\033[91;1;4m{message}\033[0m"
        super().__init__(self.message)


def line_diff_error_message(output_file: str, line_number: int, line: str, expected_line: str) -> str:
    return (
        f"\033[91mError at line {line_number} in file '{output_file}':\n"
        f"\toutput is \033[90m'{line}'\033[91m but expected: "
        f"\033[90m'{expected_line}'\033[0m"
    )


def line_diff(line: str, expected_line: str, context: dict) -> bool:
    if line == expected_line:
        return True

    print(
        line_diff_error_message(
            context["output_file"], context["line_number"], line, expected_line
        ),
        file=sys.stderr,
    )
    return False


def check_output(output_file_name: str, expected_output_file_name: str) -> float:
    with open(output_file_name, "r", encoding="utf-8") as output_file:
        output = output_file.read().splitlines()
    with open(expected_output_file_name, "r", encoding="utf-8") as expected_output_file:
        expected_output = expected_output_file.read().splitlines()

    sum_matches = 0

    for index, expected_line in enumerate(expected_output):
        actual_line = output[index] if index < len(output) else ""
        if line_diff(
            actual_line,
            expected_line,
            {"output_file": output_file_name, "line_number": index + 1},
        ):
            sum_matches += 1

    if len(output) != len(expected_output):
        print(
            f"\033[91mLine count mismatch in '{output_file_name}': "
            f"got {len(output)}, expected {len(expected_output)}\033[0m",
            file=sys.stderr,
        )

    if not expected_output:
        return 1.0 if not output else 0.0

    return sum_matches / len(expected_output)


def resolve_executable(executable: str) -> str:
    if os.path.isabs(executable):
        return executable
    return os.path.abspath(executable)


def run(executable: str, input_file_name: str, output_file_name: str, expected_output_file_name: str) -> float:
    with open(input_file_name, "r", encoding="utf-8") as input_file, open(
        output_file_name, "w", encoding="utf-8"
    ) as output_file:
        process = Popen([resolve_executable(executable)], stdout=PIPE, stdin=PIPE, text=True)

        for line_number, raw_line in enumerate(input_file, start=1):
            prompt = process.stdout.read(2)
            if prompt != "» ":
                raise CustomError(
                    f"Expected prompt '» ' before line {line_number}, got {prompt!r}"
                )

            command = raw_line.rstrip("\n")
            process.stdin.write(command + "\n")
            process.stdin.flush()

            if command == "Exit":
                break

            output_line = process.stdout.readline()
            if output_line == "":
                raise CustomError(
                    f"Process terminated before producing output for line {line_number}"
                )
            output_file.write(prompt + output_line)

        process.stdin.close()
        process.stdout.close()
        process.wait(timeout=5)

    return check_output(output_file_name, expected_output_file_name)


if __name__ == "__main__":
    if len(sys.argv) != 5:
        print("Usage: python3 checker.py <executable> <input_file> <output_file> <expected_output_file>")
        sys.exit(1)

    grade = run(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
    print(grade)
