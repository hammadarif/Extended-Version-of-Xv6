# Extended Version of xv6

An extended variant of the xv6 educational operating system, enhanced with additional features and modules.

## About

This repository is an extended/modified version of the **xv6** operating system (a teaching OS modeled after Unix Version 6). It includes custom enhancements, extra modules, networking (via lwIP), filesystem tools, and user programs beyond the stock xv6.

## Features / Extensions

Some of the added or modified components (compared to vanilla xv6) are:

- Integration of **lwIP** (lightweight IP stack) for networking  
- Additional user-space programs in `user/`  
- Modified or enhanced kernel modules in `kernel/`  
- Custom filesystem tool(s) in `mkfs/`  
- Support for client-side components in `client/`  
- Other extensions (please list yours here)  

If you want, I can help you list *all* your custom features clearly.

## Directory Structure

```text
/
├── client/        # client-side code / networking client programs
├── kernel/        # kernel source and modifications to xv6
├── lwip/          # networking stack integration
├── mkfs/          # filesystem building tools
├── user/          # user-space programs / utilities
├── .vscode/        # IDE settings (optional)
├── Makefile        # build system
├── LICENSE  
├── README.md  
