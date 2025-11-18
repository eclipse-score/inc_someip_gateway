# gateway

[`gateway`](gateway/) defines the core SOME/IP gateway logic and plugin interfaces.

`gateway` uses `socom` to define a plugin interface for the SOME/IP network access plugin and payload transformation plugin.
Via `socom` both plugin interfaces are decoupled from each other.

# socom

The interface of [`socom`](socom/) is an abstraction of SOME/IP where payloads are treated as binary data.
`socom` has a plugin API to register bridges, which transfer the `socom` / SOME/IP semantics either over the network (actual SOME/IP) or over IPC.
