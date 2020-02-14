# This library

This library demonstrates a design where each component is compiled to shared-objects.

I am demonstrating  the following claims:
- The components internally can use threads or processes.
- The components can easily defer to the network.
- The components are hot-swappable.
- The components can be compiled separately from the runtime (henceforth RT).
- The components can be tested independently.
- Other languages (Python) can easily be used.

I am still in the process of demonstrating the following claims:
- A component can be killed, and all of its threads (and other resources) will be cleaned up.
- A crash in one component does not affect the others.
- Python can be used by multiple components.

## Structure

- slam1, slam2, imu1, and cam1
  - Various component implementations (would be third-party)
- runtime
  - Our runtime that instantiates a bunch of components and implements the OpenXR spec.
- common
  - The public interface that component-authors conform to.

In production, each directory directly under the root would be its own repository (probably written by different people). There is a "common" repository documenting the public interface. In production, other repositories includ this as a git submodule rather than a symlink.

## How does it work

Component-authors write their own code which gets dynamically loaded by the RT. There is only one method which must be dynamically called: the create method. Once the component-author's object is created in the RT, the rest of the component-author's methods are called by the object's vtable since they are virtual.
