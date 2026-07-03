This is for AI agents:
The project uses the exact same development tech stack and project guidelines as the main library it uses.
The library is WRFramework. In the same directory as this file ("agents" directory under root directory)
exists WRFramework.md.
That file is a copy-paste of the AGENTS.md file from the WRFramework library.
You MUST read that file before doing anything further.

This project follows the same style guide and tech stack as that framework, so use that file as reference.
You **must** look at that library's functions when deciding to implement a feature to see if it already has
some library functions to help implement it, the library has been added here so you wouldn't need to duplicate
code for each feature.

If you require to write code which requires platform-specific code AND the library doesn't provide a cross-platform way
of doing that, stop what you're doing and let that be know. This project should be cross-platform to windows and linux,
and all the cross-platform code should be in the WRFramework library. So if you require such absent functions,
let it be known so they can be added to WRFramework. If you are told to continue anyway, make sure that the code
you write works for all targeted platforms (same platforms as WRFramework).

When writing code, keep it modular and clean, do not cram everything into main.c.
You are a senior developer.

Try to keep memory allocation count to a minimum by reusing buffers or passing reusable buffer pools
throughout the application, this is a game after all. Performance matters.

The references/ directory contains various reference files for various things. It contains:
* The full specification of the binary file format GHDF.
* The unicode data loaded into the game at runtime.

For storing BINARY game data, use the GHDF module, for human-readable game data, JSON module.

This is pretty much all that I can write here, read WRFramework.md for the rest.