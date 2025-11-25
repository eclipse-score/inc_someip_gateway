# TODO - SOME/IP Gateway Project

## 🚀 Quick Wins (Small Tasks)
**Purpose**: Small, self-contained tasks that can be completed in under 1 hour. Perfect for filling gaps between larger work or when you have limited time available.

**Guidelines**: Keep tasks simple, well-defined, and requiring minimal context switching.

- [ ] Add missing documentation headers to source files
- [ ] Fix linting issues in existing codebase
- [ ] Update copyright headers for 2025
- [ ] Add basic unit test coverage report
- [ ] Get proper service IDs for the IPC communication


## 🔥 High Priority
**Purpose**: Critical tasks that are blocking progress or essential for basic functionality. These should be addressed before other work.

**Guidelines**: Focus on items that unblock other team members or are required for project milestones.

- [ ] Define service for registering gateways


## 🛠️ Development Tasks
**Purpose**: Main feature development and implementation work. Organized by category for better project planning.

**Guidelines**: Break down large features into smaller, manageable tasks. Include acceptance criteria where helpful.

### Core Features
- [ ] Implement SOME/IP header parsing
- [ ] Add SOME/IP payload handling
- [ ] Create service discovery mechanism
- [ ] Implement request/response handling
- [ ] Add publish/subscribe support

### Code Quality
- [ ] Implement comprehensive error handling

### Technical Debt
- [ ] Use score logging
- [ ] Refactor prototype code into a better architecture

### Build & Dependencies
- [ ] Optimize Bazel build configuration
    - [x] Build vsomeip using native bazel or at least improve the Boost dependency
    - [ ] Right now the clang++ from the environment is somehow visible in the sandbox when building Boost which also makes it necessary that you have clang installed on your host. We should improve the sandboxing so that clang from the host system is no longer visible to any build.
- [ ] Update dependency versions
- [ ] Try out running on QNX
    - There could be an issue with the typeid operator which is used by Boost ASIO.
      Check with @JochenSatETAS


## 🔍 Investigation & Research
**Purpose**: Items requiring research, analysis, or exploration before implementation. Use this for parking ideas that need more investigation.

**Guidelines**: Include context about why investigation is needed and what questions to answer. Set timeboxes for research tasks.

- [ ] Check how to modify the ["network" configuration option](https://github.com/COVESA/vsomeip/blob/master/documentation/vsomeipConfiguration.md#network) to allow multiple someipds run in parallel (for load balancing, TSN, etc.)
- [ ] Investigate alternative SOME/IP libraries for better performance
- [ ] Research optimal threading model for gateway


## 🐛 Bug Fixes
**Purpose**: Known issues, defects, and problems that need to be resolved. Separate from features to maintain focus on stability.

**Guidelines**: Include steps to reproduce, expected vs actual behavior, and impact assessment when possible.

- [ ]


## 📚 Documentation
**Purpose**: Improve project documentation for better developer experience and knowledge sharing.

**Guidelines**: Focus on user-facing docs, API references, and guides that help onboard new contributors or users.

- [ ] Write comprehensive README
- [ ] Create API documentation
- [ ] Add architecture diagrams
- [ ] Write integration guide
- [ ] Create troubleshooting guide
- [ ] Add examples and tutorials


## 🧪 Testing
**Purpose**: Improve test coverage, quality, and reliability. Essential for maintaining code quality as the project grows.

**Guidelines**: Prioritize testing for critical paths and complex logic. Include both positive and negative test cases.

- [ ] Implement unit tests for core components
- [ ] Create integration test suite
- [ ] Add performance tests
- [ ] Set up stress testing
- [ ] Create mock SOME/IP services for testing
- [ ] Add regression test suite


## 🚢 Deployment & Operations
**Purpose**: Tasks related to production deployment, monitoring, and operational readiness.

**Guidelines**: Focus on making the project production-ready with proper observability and reliability measures.

- [ ] Create deployment scripts
- [ ] Set up monitoring and logging
- [ ] Create health check endpoints


## 🔮 Future Ideas
**Purpose**: Long-term goals, nice-to-have features, and innovative ideas that aren't immediately needed but could add value.

**Guidelines**: Think big picture and user experience improvements. These can be revisited during planning sessions or when capacity allows.

- [ ] Add support for dynamic serialization (e.g. via flatbuffers)
- [ ] Add support for different transport protocols
- [ ] Add support for configuration hot-reloading
- [ ] Implement advanced routing capabilities
- [ ] Add support for message transformation
- [ ] Implement request/response handling
