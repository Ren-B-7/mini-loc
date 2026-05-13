import json
import sys


def convert(input_file, output_file):
    with open(input_file, "r") as f:
        # Load the JSON as an object/dict directly
        data = json.load(f)

    # Encode the original JSON data directly to bytes
    json_str = json.dumps(data, separators=(",", ":")).encode("utf-8")

    # Write to C header
    with open(output_file, "w") as f:
        f.write("static const unsigned char languages_json[] = {")
        for i, b in enumerate(json_str):
            if i % 12 == 0:
                f.write("\n    ")
            f.write(f"0x{b:02x}, ")
        f.write("0x00\n};\n")
        f.write("static const size_t languages_json_len = " + str(len(json_str)) + ";")


if __name__ == "__main__":
    convert("assets/languages.json", "src/include/languages_data.h")
