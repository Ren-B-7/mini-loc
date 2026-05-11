# mini-loc

`mini-loc` is an ultra-fast, minimal tool designed to index codebases. It is built for raw performance in C, making it an ideal choice for quickly scanning large project directories to count lines of code, comments, and blank lines.

It is single threaded. Since spawning more threads will likely explode the time with more memory overhead and time needed to rejoin threads.

## Performance

Built with speed in mind, `mini-loc` handles massive codebases in sub-second times.
(Ran on an i7-1156G7; 24 GBs of ram; On an m.2 NVME)
| Target | Indexing Time |
| :--- | :--- |
| **Linux Kernel** | ~1.1 seconds |
| **Node.js** | ~0.5 seconds |
| **Ladybird** | ~0.1 seconds |
| **Rust** | ~0.3 seconds |
| **Vscode** | ~0.5 seconds |
| **Pi-hole** | ~0.01 seconds |


![Performance Breakdowns Linux + Rust](Linux_and_Rust_index.png)
![Performance Breakdowns Ladybird + Node](Ladybird_and_Node_index.png)
![Performance Breakdowns Vscode + Pihole](Vscode_and_Pihole_index.png)

## Building

This project uses a `Makefile` for building and managing the project. Ensure you have `gcc`, `make`, `clang-format`, and `clang-tidy` installed.

### Build the project

To compile the source code, run:

```bash
make
```

The resulting binary will be located in the `bin/` directory.

### Cleaning

To remove all build artifacts and the binary, run:

```bash
make clean
```

### Installation

You can install `mini-loc` to your `~/.local/bin` directory:

```bash
make install
```

To uninstall:

```bash
make uninstall
```

## Usage

Point the program at a directory to begin indexing:

```bash
loc -r /path/to/codebase
```

## License

This project is open-source and licensed under the [MIT License](LICENSE).
