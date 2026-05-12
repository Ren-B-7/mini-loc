import json
import sys


def convert(input_file, output_file):
    with open(input_file, "r") as f:
        data = json.load(f)
        langs = []
        for i in data:
            if isinstance(i, list):
                langs.extend(i)
            else:
                langs.append(i)

    # Write to C header
    with open(output_file, "w") as f:
        json_str = json.dumps(langs).encode("utf-8")
        f.write("const unsigned char languages_json[] = {")
        for i, b in enumerate(json_str):
            if i == 0:
                f.write(f"0x{b:02x}, ")
            elif i % 6 == 0:
                f.write("\n    0x{b:02x}, ".format(b=b))
            else:
                f.write(f"0x{b:02x}, ")
        f.write("0x00\n};\n")
        f.write("const size_t languages_json_len = " + str(len(json_str)) + ";")


if __name__ == "__main__":
    convert("assets/languages.json", "src/include/languages_data.h")
