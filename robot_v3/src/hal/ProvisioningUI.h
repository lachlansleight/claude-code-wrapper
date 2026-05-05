#pragma once

/**
 * @file ProvisioningUI.h
 * @brief Glue between Provisioning and Display.
 *
 * Trivial bridge module — the portal needs to surface its AP SSID +
 * IP to the user, and the only such surface on the device is the TFT.
 * Keeping the wiring out of Provisioning itself avoids pulling Display
 * into the HAL credentials layer.
 */
namespace ProvisioningUI {

/**
 * Register a Provisioning::onPortalState handler that calls
 * Display::drawPortalScreen(). Call once during setup, after
 * Display::begin() and before any path that might call
 * Provisioning::runPortal().
 */
void begin();

}  // namespace ProvisioningUI
