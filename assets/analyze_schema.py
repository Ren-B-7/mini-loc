import json


def get_unique_keys(data, keys=None):
    if keys is None:
        keys = set()

    if isinstance(data, dict):
        for key, value in data.items():
            keys.add(key)
            get_unique_keys(value, keys)
    elif isinstance(data, list):
        for item in data:
            get_unique_keys(item, keys)

    return keys


def main():
    try:
        with open("assets/languages.json", "r") as f:
            data = json.load(f)

        unique_keys = get_unique_keys(data)

        print("Unique keys found in languages.json:")
        for key in sorted(unique_keys):
            print(f"- {key}")

    except Exception as e:
        print(f"Error: {e}")


if __name__ == "__main__":
    main()
