#pragma once
/// @file Products.h
/// @brief Unified include for all data product contracts.

#ifndef VSGRAYSIM_PRODUCTS_H
#define VSGRAYSIM_PRODUCTS_H

#include "vsgraysim/products/TerrainProduct.h"
#include "vsgraysim/products/SceneProduct.h"
#include "vsgraysim/products/EquipmentStateProduct.h"
#include "vsgraysim/products/DisplayFrameProduct.h"

namespace vsgraysim {

// Convenience type aliases
using TerrainProductPtr = std::shared_ptr<TerrainProduct>;
using TerrainProductConstPtr = std::shared_ptr<const TerrainProduct>;
using SceneProductPtr = std::shared_ptr<SceneProduct>;
using SceneProductConstPtr = std::shared_ptr<const SceneProduct>;
using EquipmentStateProductPtr = std::shared_ptr<EquipmentStateProduct>;
using DisplayFrameProductPtr = std::shared_ptr<DisplayFrameProduct>;

} // namespace vsgraysim

#endif // VSGRAYSIM_PRODUCTS_H