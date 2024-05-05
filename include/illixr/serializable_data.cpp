//
// Created by steven on 11/5/23.
//

#include "serializable_data.hpp"

CEREAL_REGISTER_DYNAMIC_INIT()

CEREAL_REGISTER_TYPE(ILLIXR::compressed_frame)
CEREAL_REGISTER_POLYMORPHIC_RELATION(ILLIXR::switchboard::event, ILLIXR::compressed_frame)
CEREAL_REGISTER_TYPE(ILLIXR::pose_type)
CEREAL_REGISTER_POLYMORPHIC_RELATION(ILLIXR::switchboard::event, ILLIXR::pose_type)
CEREAL_REGISTER_TYPE(ILLIXR::fast_pose_type)
CEREAL_REGISTER_POLYMORPHIC_RELATION(ILLIXR::switchboard::event, ILLIXR::fast_pose_type)