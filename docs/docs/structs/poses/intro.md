# Poses in ILLIXR

Poses are one of the fundamental types of data used by ILLIXR. The pose represents the position (x, y, z) and orientation (a quaternion: w, x, y, z) of an object. ILLIXR can work with three different forms of the pose:

  - ILLIXR internal format: [API][A10]
      - x, y, z are contained in an Eigen vector
      - w, x, y, z are contained in an Eigen Quaternion
      - This format is designed for performing direct computations on the components (e.g., shift, rotation)
  - OpenXR format (XrPosef): [API][A30]
      - x, y, z are contained in a vector of three float values
      - w, x, y, z are contained in a vector of four float values
      - This format is designed for transport and compatability, with no inherent overheads
  - Monado (xrt_pose): [API][A50]
      - x, y, z are contained in a vector of three float values
      - w, x, y, z are contained in a vector of four float values
      - This format is bit-identical in structure to the OpenXR format, but the compiler sees it as a distinct type, so formal copy/conversion is necessary between the two

## Pose Types

### Head Pose

#### Head Pose Data

The `head_pose_data` struct is specific to ILLIXR's internal hand tracking and will be retired in the near future.

#### Head Pose Type

The `head_pose_type` is used to convey both the pose and related velocity information (linear and angular); along with validity flags. OpenXR itself does not have a single structure that encapsulates all of this data, so we adopt the Monado `xrt_space_relation` struct for this purpose. So that Monado is not required just for this struct, we have our own (structurally equivalent) definition in `openxr_defines.hpp`.

  - ILLIXR: [API][A11]
  - OpenXR: [API][A31]
  - Monado: [API][A51]

#### Fast Head Pose Type

The `fast_head_pose_type` is designed to be used as the output of pose prediction. In addition to the a `head_pose_type` it carries members for predicted pose time and the computed time. This is the only serializable form of the head pose, enabling it to be sent over a network.

  - ILLIXR: [API][A12]
  - OpenXR: [API][A32]
  - Monado: [API][A52]

### Hand Poses

The hand pose structs carry information about the 26 points used to describe the position of each hand and fingers.

#### Hand Joint Pose

The `hand_joint_pose` contains the pose, velocities (linear and angular), and validity flags for a single joint of the hand. For Monado we adopt the `xrt_hand_joint_value`; for OpenXR this is a structurally equivalent extension of the `xrt_space_relation` struct with a joint radius float. 

  - ILLIXR: currently undergoing adoption
  - OpenXR: [API][A33]
  - Monado: [API][A53]

#### Hand Joint Poses

The `hand_joint_poses` structure holds the `hand_joint_pose` instances for all joints of a single hand along with a flag indicating if the hand was detected. For Monado we use the `xrt_hand_joint_set`, while for OpenXR we use the structurally equivalent `hand_joint_poses`.

  - ILLIXR: currently undergoing adoption
  - OpenXR: [API][A34]
  - Monado: [API][A54]

#### Hand Joint Poses Pair

The `hand_joint_poses_pair` structure holds a map of the hands (RIGHT and LEFT) to their respective `hand_joint_poses` data, along with a timestamp to indicate the time the poses were acquired. This is the only serializable form of the hand poses, enabling it to be sent over a network. The OpenXR and Monado versions are identical.

  - ILLIXR: currently undergoing adoption
  - OpenXR: [API][A35]
  - Monado: [API][A55]

### Palm Poses

The palm pose structs hold information pertaining to the position, orientation, and movement of the palm.

#### Palm Pose

The `palm_pose` struct inherits from the `xrt_space_relation` and adding a function to determine if the data it contains are valid.

  - ILLIXR: currently undergoing adoption
  - OpenXR: [API][A36]
  - Monado: [API][A56]

#### Palm Poses Pair

The `palm_poses_pair` struct holds a mapping of hand (RIGHT or LEFT) to its associated `palm_pose` data and a `time_point` for the timestamp of the data. The Openxr and Monado versions are structurally equivalent. This is the only serializable form for of the palm pose, enabling it to be sent over a network.

  - ILLIXR: currently undergoing adoption
  - OpenXR: [API][A37]
  - Monado: [API][A57]

### Hand Interactions

Hand interactions carry information about basic hand gestures: poke, grab, aim, and pinch.

#### Hand Interaction Pose

The `hand_interaction_pose` struct contains data about whether a given hand interaction is being performed, where it is being performed, and whether the runtime considers the gesture to be active. The struct inherits from `xrt_space_relation`. The OpenXR and Monado versions are structurally equivalent.

  - ILLIXR: currently undergoing adoption
  - OpenXR: [API][A38]
  - Monado: [API][A58]

#### Hand Interaction Poses

The `hand_interaction_poses` structure maps the pose type (AIM, GRIP, PINCH, POKE) to their `hand_interaction_pose` data. The OpenXR and Monado versions are structurally equivalent.

  - ILLIXR: currently under adoption
  - OpenXR: [API][A39]
  - Monado: [API][A59]

#### Hand Interation Poses Pair

The `hand_interaction_poses_pair` structure maps the hand interactions to specific hands (LEFT and RIGHT). It is the only serializable version of the hand interactions, enabling it to be sent over a network.

  - ILLIXR: currently undergoing adoption
  - OpenXR: [API][A40]
  - Monado: [API][A60]

### Combined Pose

The `combined_pose` structure encapsulates all pose information (head, hand, hand interactions, palm) from a specific instance in time. It also holds some network latency information to enable better pose prediction on the server. It is designed to carry off of this information over a network.

  - ILLIXR: currently undergoing adoption
  - OpenXR: [API][A41]
  - Monado: [API][A61]


[//]: # (- API -)

[A10]: ../../api/structILLIXR_1_1data__format_1_1pose_1_1pose__base.md

[A11]: ../../api/structILLIXR_1_1data__format_1_1pose_1_1head__pose__type.md

[A12]: ../../api/structILLIXR_1_1data__format_1_1pose_1_1fast__head__pose__type.md



[A30]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1pose__base.md

[A31]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1xrt__space__relation.md

[A32]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1fast__head__pose__type.md

[A33]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1hand__joint__pose.md

[A34]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1hand__joint__poses.md

[A35]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1hand__joint__poses__pair.md

[A36]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1palm__pose.md

[A37]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1palm__poses__pair.md

[A38]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1hand__interaction__pose.md

[A39]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1hand__interaction__poses.md

[A40]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1palm__poses__pair.md

[A41]: ../../api/oxr/structILLIXR_1_1data__format_1_1pose_1_1combined__pose.md


[A50]: ../../api/oxr_monado/structILLIXR_1_1data__format_1_1pose_1_1pose__base.md

[A51]: https://monado.pages.freedesktop.org/monado/structxrt__space__relation.html

[A52]: ../../api/oxr_monado/structILLIXR_1_1data__format_1_1pose_1_1fast__head__pose__type.md

[A53]: https://monado.pages.freedesktop.org/monado/structxrt__hand__joint__value.html

[A54]: https://monado.pages.freedesktop.org/monado/structxrt__hand__joint__set.html

[A55]: ../../api/oxr_monado/structILLIXR_1_1data__format_1_1pose_1_1hand__joint__poses__pair.md

[A56]: ../../api/oxr_monado/structILLIXR_1_1data__format_1_1pose_1_1palm__pose.md

[A57]: ../../api/oxr_monado/structILLIXR_1_1data__format_1_1pose_1_1palm__poses__pair.md

[A58]: ../../api/oxr_monado/structILLIXR_1_1data__format_1_1pose_1_1hand__interaction__pose.md

[A59]: ../../api/oxr_monado/structILLIXR_1_1data__format_1_1pose_1_1hand__interaction__poses.md

[A60]: ../../api/oxr_monado/structILLIXR_1_1data__format_1_1pose_1_1palm__poses__pair.md

[A61]: ../../api/oxr_monado/structILLIXR_1_1data__format_1_1pose_1_1combined__pose.md
