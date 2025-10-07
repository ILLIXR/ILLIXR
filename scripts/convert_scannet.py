from math import sqrt
import click
import numpy
import numpy.typing as npt
import os
from PIL import Image


# Convert JPG images into PNG images
def write_png_images(source_dir: str):
    print("Converting color images from JPG to PNG")
    for filename in sorted(os.listdir(source_dir)):
        if filename.endswith(".color.jpg"):
            newfilename = filename[:-10] + ".color.png"
            img = Image.open(source_dir + "/" + filename)
            img.save(source_dir + "/" + newfilename)


# Extract translation from an SE3 matrix
def matrix_to_translation(pose: npt.ArrayLike) -> npt.ArrayLike:
    # First three columns of the last row are the translation
    return pose[3, 0:3]


# Get matrix variant for quaternion conversion
def get_matrix_variant(rotation_matrix: npt.ArrayLike) -> int:
    if (rotation_matrix[1, 1] > -rotation_matrix[2, 2]) and (rotation_matrix[0, 0] > -rotation_matrix[1, 1]) and (
            rotation_matrix[0, 0] > -rotation_matrix[2, 2]):
        return 0
    elif (rotation_matrix[1, 1] < -rotation_matrix[2, 2]) and (rotation_matrix[0, 0] > rotation_matrix[1, 1]) and (
            rotation_matrix[0, 0] > rotation_matrix[2, 2]):
        return 1
    elif (rotation_matrix[1, 1] > rotation_matrix[2, 2]) and (rotation_matrix[0, 0] < rotation_matrix[1, 1]) and (
            rotation_matrix[0, 0] < -rotation_matrix[2, 2]):
        return 2
    elif (rotation_matrix[1, 1] < rotation_matrix[2, 2]) and (rotation_matrix[0, 0] < -rotation_matrix[1, 1]) and (
            rotation_matrix[0, 0] < rotation_matrix[2, 2]):
        return 3
    else:
        return 0


# Extract quaternion (w, x, y, z) from an SE3 matrix
def matrix_to_quaternion(pose: npt.ArrayLike) -> npt.ArrayLike:
    # Rotation matrix is top-left 3x3 matrix
    rotation_matrix = pose[0:3, 0:3]

    variant = get_matrix_variant(rotation_matrix=rotation_matrix)
    denom = 1.0

    # The following is verbatim from ITMBasicEngine.tpp:QuaternionFromRotationMatrix()
    if variant == 0:
        denom += rotation_matrix[0, 0] + rotation_matrix[1, 1] + rotation_matrix[2, 2]
    elif variant == 1:
        denom += rotation_matrix[0, 0]
        denom -= rotation_matrix[1, 1]
        denom -= rotation_matrix[2, 2]
    elif variant == 2:
        denom += rotation_matrix[1, 1]
        denom -= rotation_matrix[2, 2]
        denom -= rotation_matrix[0, 0]
    elif variant == 3:
        denom += rotation_matrix[2, 2]
        denom -= rotation_matrix[0, 0]
        denom -= rotation_matrix[1, 1]

    denom = sqrt(denom)

    quat = numpy.empty(shape=4)
    quat[variant] = 0.5 * denom

    denom *= 2.0

    if variant == 0:
        quat[1] = (rotation_matrix[1, 2] - rotation_matrix[2, 1]) / denom
        quat[2] = (rotation_matrix[2, 0] - rotation_matrix[0, 2]) / denom
        quat[3] = (rotation_matrix[0, 1] - rotation_matrix[1, 0]) / denom
    elif variant == 1:
        quat[0] = (rotation_matrix[1, 2] - rotation_matrix[2, 1]) / denom
        quat[2] = (rotation_matrix[0, 1] + rotation_matrix[1, 0]) / denom
        quat[3] = (rotation_matrix[2, 0] + rotation_matrix[0, 2]) / denom
    elif variant == 2:
        quat[0] = (rotation_matrix[2, 0] - rotation_matrix[0, 2]) / denom
        quat[1] = (rotation_matrix[0, 1] + rotation_matrix[1, 0]) / denom
        quat[3] = (rotation_matrix[1, 2] + rotation_matrix[2, 1]) / denom
    elif variant == 3:
        quat[0] = (rotation_matrix[0, 1] - rotation_matrix[1, 0]) / denom
        quat[1] = (rotation_matrix[2, 0] + rotation_matrix[0, 2]) / denom
        quat[2] = (rotation_matrix[1, 2] + rotation_matrix[2, 1]) / denom

    if quat[0] < 0.0:
        quat *= -1.0

    return quat


# Generate pose file
def write_pose_file(source_dir: str):
    print("Generating pose file")
    poses = []
    depths = []
    colors = []

    # The following code only works if the file list is sorted. The assumption is that for a given frame,
    # its pose, depth, and color will be processed together.
    for filename in sorted(os.listdir(source_dir)):
        if filename.endswith(".pose.txt"):
            pose = numpy.loadtxt(open(source_dir + "/" + filename, "r"), delimiter=" ", skiprows=0)
            poses.append(pose.transpose())
        elif filename.endswith(".depth.pgm"):
            depths.append(str(filename))
        elif filename.endswith(".color.png"):
            colors.append(str(filename))

    # Write pose file line by line. Each line's format is:
    # timestamp tx ty tz qx qy qz qw timestamp depth timestamp color
    with open(source_dir + "/../poses/groundtruth.txt", "w") as output:
        for i in range(len(poses)):
            # Skip when pose is infinity/unavailable
            if numpy.isinf(poses[i]).any():
                continue

            translation = matrix_to_translation(poses[i])
            quaternion = matrix_to_quaternion(poses[i])
            line = str(i) + " " + str(translation[0]) + " " + str(translation[1]) + " " + str(translation[2]) + " " + \
                   str(quaternion[1]) + " " + str(quaternion[2]) + " " + str(quaternion[3]) + " " + str(quaternion[0]) + " " + \
                   str(i) + " " + "images/" + depths[i] + " " + \
                   str(i) + " " + "images/" + colors[i] + "\n"
            output.write(line)


# Generate calibration file
def write_calibration_file(source_dir: str):
    print("Generating calibration file")
    with open(source_dir + "/_info.txt", "r") as calibration_file:
        lines = calibration_file.readlines()

        # Remove whitespace and split on "=" to get each calibration entry
        lines = [line.strip().split(sep="=") for line in lines]

        # Remove whitespace from each entry
        lines = [[entry.strip() for entry in line] for line in lines]

        # Unfortunately, this format isn't documented anywhere
        color_width = lines[2][1]
        color_height = lines[3][1]
        depth_width = lines[4][1]
        depth_height = lines[5][1]
        depth_shift = float(lines[6][1])
        color_intrinsic = lines[7][1].split()
        color_extrinsic = lines[8][1].split()
        depth_intrinsic = lines[9][1].split()

        # Write calibration file in InfiniTAM's format
        with open(source_dir + "/../calibration.txt", "w") as output:
            # Color camera intrinsics
            output.write(color_width + " " + color_height + "\n")
            output.write(color_intrinsic[0] + " " + color_intrinsic[5] + "\n")
            output.write(color_intrinsic[2] + " " + color_intrinsic[6] + "\n")
            output.write("\n")

            # Depth camera intrinsics
            output.write(depth_width + " " + depth_height + "\n")
            output.write(depth_intrinsic[0] + " " + depth_intrinsic[5] + "\n")
            output.write(depth_intrinsic[2] + " " + depth_intrinsic[6] + "\n")
            output.write("\n")

            # Color to depth extrinsic transformation
            output.write(color_extrinsic[0] + " " + color_extrinsic[1] + " " + color_extrinsic[2] + " " + color_extrinsic[3] + "\n")
            output.write(color_extrinsic[4] + " " + color_extrinsic[5] + " " + color_extrinsic[6] + " " + color_extrinsic[7] + "\n")
            output.write(color_extrinsic[8] + " " + color_extrinsic[9] + " " + color_extrinsic[10] + " " + color_extrinsic[11] + "\n")
            output.write("\n")

            # Depth transform
            output.write("affine " + str(1.0 / depth_shift) + " 0.0\n")


if __name__ == "__main__":

    @click.command(no_args_is_help=True)
    @click.option("--source-dir", type=str, help="ScanNet input directory")
    def main(source_dir: str):
        write_png_images(source_dir=source_dir)
        write_pose_file(source_dir=source_dir)
        write_calibration_file(source_dir=source_dir)

    main()
