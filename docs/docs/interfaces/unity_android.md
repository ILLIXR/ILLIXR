# Unity Android Interface

The Unity Android interface gives a Unity App access to read and write certain topics in ILLIXR's [switchboard][I10].
The specific code of this interface was designed to work in conjunction with the [semantic_python][I11] plugin. It should
be easily modifiable to work with other plugins as well. The interface has been tested on a Quest 3 headset with the
[SemanticXR][L10] Unity app.

## Structure

The interface has two distinct parts:

- Unity bridge
- Unity component

### Unity Bridge

Since Unity is the app running on the device, ILLIXR cannot be easily started as an independent app and have all the 
access it needs. The Unity bridge provides functions pointers for C# to call into the [quest3.unity][I12] plugin and
creates a library loaded by the Unity app. This library starts the ILLIXR system, which in turn loads the required plugins.

### Unity Component

`unity_component` is an ILLIXR plugin whose sole purpose is to give Unity access to certain [switchboard][I10] topics.
This allows the app to read from the `semantic_response` topic and write to the `semantic_query` topic. The functions
themselves translate to and from large argument lists and C++ structs, which are used by the topics.


[//]: # (- Internal -)

[I10]:  ../glossary.md#switchboard

[I11]:  ../plugin_README/README_semantic_python.md

[I12]:  ../plugin_README/README_quest3_unity.md

[L10]:  https://github.com/ILLIXR/SemanticXR/blob/illixr/integration
