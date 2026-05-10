import json
import sys


def convert(input_file, output_file):
    with open(input_file, "r") as f:
        langs = json.load(f)

    # Write to C header
    with open(output_file, "w") as f:
        json_str = json.dumps(langs).encode("utf-8")
        f.write("const unsigned char languages_json[] = {")
        for i, b in enumerate(json_str):
            if i % 12 == 0:
                f.write("\n\t")
            f.write(f"0x{b:02x}, ")
        f.write("0x00\n};\n")
        f.write("const size_t languages_json_len = " + str(len(json_str)) + ";")


if __name__ == "__main__":
    convert("assets/languages.json", "src/include/languages_data.h")
