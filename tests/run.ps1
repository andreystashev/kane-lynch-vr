# Invoked by build-vr.ps1 after building the runtime.
function TestSource([string]$Name) {
    Join-Path $root "tests/native/$Name.cpp"
}

function Test-PythonSuite([string]$Pattern, [string]$FailureMessage) {
    $previous = $env:KL1_TEST_GAME_EXE
    try {
        $env:KL1_TEST_GAME_EXE = $GameExe
        & python -X utf8 -m unittest discover -s (Join-Path $root 'tests/tools') -p $Pattern
        if ($LASTEXITCODE) { throw $FailureMessage }
    } finally {
        $env:KL1_TEST_GAME_EXE = $previous
    }
}

Write-Host "== Self tests =="
& "$dist\LaunchVR.exe" --self-test
if($LASTEXITCODE -ne 0){ throw 'Launcher self-test failed' }
Write-Host '== Exact owned-patch and native startup compatibility (focused CPU fixtures) =='
$startupManifestObjects=@{}
foreach($architecture in @('x86','x64')) {
    $proofPrefix=if($architecture -eq 'x86'){'i686'}else{'x86_64'}
    $proofWindres=Join-Path $bin "$proofPrefix-w64-mingw32-windres.exe"
    $startupManifestObject=Join-Path $cache "test-as-invoker-$architecture.obj"
    & $proofWindres -I (Join-Path $root 'tests/fixtures') -i (Join-Path $root 'tests\fixtures\test_as_invoker.rc') -o $startupManifestObject -O coff
    if($LASTEXITCODE -ne 0){throw "CPU test asInvoker resource compile failed: $architecture"}
    $startupManifestObjects[$architecture]=$startupManifestObject
}
foreach($testSource in @('owned_patch_page_audit_test','launcher_native_startup_test')) {
    foreach($architecture in @('x86','x64')) {
        $proofCompiler=if($architecture -eq 'x86'){$clang32}else{$clang64}
        $testExe=Join-Path $cache "$testSource-$architecture.exe"
        & $proofCompiler @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude (TestSource $testSource) $startupManifestObjects[$architecture] -o $testExe
        if($LASTEXITCODE -ne 0){throw "Startup ownership fixture compile failed: $testSource/$architecture"}
        & $testExe
        if($LASTEXITCODE -ne 0){throw "Startup ownership fixture failed: $testSource/$architecture"}
    }
}
$ownedPatchMetadata=Join-Path $cache 'owned_patch_metadata_test-x86.exe'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\owned_patch_metadata_test.cpp" $mh32 $startupManifestObjects['x86'] -o $ownedPatchMetadata -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Actual MinHook ownership metadata fixture compile failed'}
& $ownedPatchMetadata
if($LASTEXITCODE -ne 0){throw 'Actual MinHook ownership metadata fixture failed'}
$sdkOwnCache=Join-Path $cache 'sdk_own_cache_12_test-x86.exe'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type-mismatch -static -I $mhInclude "$root\tests\native\sdk_own_cache_12_test.cpp" $startupManifestObjects['x86'] -o $sdkOwnCache -lbcrypt
if($LASTEXITCODE -ne 0){throw 'SDK owned-page transaction fixture compile failed'}
& $sdkOwnCache
if($LASTEXITCODE -ne 0){throw 'SDK owned-page transaction fixture failed'}
foreach($testSource in @('physical_reload_kl_test','physical_reload_runtime_kl_test','reload_slide_capture_kl_test','reload_body_pocket_kl_test','native_aim_target_policy_test','native_marker_source_kl_test','native_team_assist_kl_test','native_mounted_aim_kl_test','native_carried_camera_math_test','native_camera_math_test','native_camera_kl_math_test','native_cinematic_kl_test','rappel_look_math_test','native_loading_lifecycle_test','native_ledge_kl_test','native_hip_attachment_kl_test','native_grenade_kl_test','native_scope_kl_test','native_movie_kl_test','native_pip_kl_test','native_effects_kl_test','native_team_hud_kl_test','native_team_marker_kl_test','native_team_marker_math_test','native_world_marker_math_test','native_command_ray_kl_test','native_team_commands_kl_test','team_command_ipc_test','native_context_hud_kl_test','native_gameplay_hud_kl_test','native_hand_hud_kl_test','native_action_kl_test','native_cover_kl_test','native_body_math_test','native_weapon_math_test','native_hud_math_test','native_hud_shadow_test','audio_profile_math_test','native_player_pose_test','native_pose_transaction_test','native_rig_math_test','native_arm_helpers_test','native_skin_math_test','controller_input_tests','d3d9_abi_tests','stereo_render_math_test','hands_tests','meta_submit_pacing_test','frame_pacing_test','stereo_pair_policy_test','frame_event_trace_test','capture_frame_policy_test','frame_copy_test','frame_delivery_metrics_test','cpu_frame_ipc_test','gpu_read_lifetime_test','profile_timing_test','d3d9_gpu_timing_test','on12_queue_policy_test','d3d9_on12_pacing_test')) {
    $testExe=Join-Path $cache ($testSource+'.exe')
    & $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static (TestSource $testSource) -o $testExe
    if($LASTEXITCODE -ne 0){ throw "Test compile failed: $testSource" }
    & $testExe
    if($LASTEXITCODE -ne 0){ throw "Test failed: $testSource" }
}

Write-Host '== Pinned executable camera/scope/reload proofs (read-only) =='
$hasProofExe=$GameExe -and (Test-Path -LiteralPath $GameExe -PathType Leaf)
if($hasProofExe){
    . (Join-Path $root 'tests\fixtures\New-KL1ProofImage.ps1')
    $GameExe=New-KL1ProofImage -GameExe $GameExe -CacheRoot $cache
}
foreach($testSource in @('native_scope_overlay_kl_test','native_reload_kl_test')) {
    foreach($architecture in @('x86','x64')) {
        $proofCompiler=if($architecture -eq 'x86'){$clang32}else{$clang64}
        $testExe=Join-Path $cache "$testSource-$architecture.exe"
        & $proofCompiler @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static (TestSource $testSource) -o $testExe
        if($LASTEXITCODE -ne 0){throw "Pinned executable proof compile failed: $testSource/$architecture"}
        if($hasProofExe){& $testExe $GameExe;if($LASTEXITCODE -ne 0){throw "Pinned executable proof failed: $testSource/$architecture"}}
        else{Write-Host "SKIP $testSource/${architecture}: supply -GameExe for read-only pinned executable proof."}
    }
}


Write-Host '== Actual native camera, attachment, grenade and mounted adapters (CPU fixtures) =='
Test-PythonSuite 'test_record_vr.py' 'Continuous recorder timeline/audio/hotkey tests failed'
foreach($recordingTest in @('recording_ipc_test','recording_mirror_warp_test')) {
    $recordingExe=Join-Path $cache "$recordingTest-x64.exe"
    & $clang64 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static (TestSource $recordingTest) -o $recordingExe -ld3d11 -ldxgi
    if($LASTEXITCODE -ne 0){throw "Recorder adapter compile failed: $recordingTest"}
    & $recordingExe
    if($LASTEXITCODE -ne 0){throw "Recorder adapter failed: $recordingTest"}
}
foreach($testSource in @('native_reflection_camera_json_test')) {
    foreach($architecture in @('x86','x64')) {
        $proofCompiler=if($architecture -eq 'x86'){$clang32}else{$clang64}
        $testExe=Join-Path $cache "$testSource-$architecture.exe"
        & $proofCompiler @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static (TestSource $testSource) -o $testExe
        if($LASTEXITCODE -ne 0){throw "Reflection evidence compile failed: $architecture"}
        & $testExe
        if($LASTEXITCODE -ne 0){throw "Reflection evidence failed: $architecture"}
    }
}
$reflectionCameraProof=Join-Path $cache 'native_reflection_camera_production_test.exe'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_reflection_camera_production_test.cpp" -o $reflectionCameraProof -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Reflection producer/consumer compile failed'}
if($hasProofExe){& $reflectionCameraProof $GameExe}else{& $reflectionCameraProof}
if($LASTEXITCODE -ne 0){throw 'Reflection producer/consumer failed'}
$pipProducerProof=Join-Path $cache 'native_pip_producer_shared_production_test.exe'
# This fixture supplies fail-fast MinHook stubs and tests the ONE owned
# camera dispatcher. Do not link $mh32: it conflicts with those fixture stubs.
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_pip_producer_shared_production_test.cpp" -o $pipProducerProof -lbcrypt -ld3d9
if($LASTEXITCODE -ne 0){throw 'Shared PIP/camera producer compile failed'}
if($hasProofExe){& $pipProducerProof $GameExe;if($LASTEXITCODE -ne 0){throw 'Shared PIP/camera producer failed'}}
else{Write-Host 'SKIP shared PIP/camera producer: supply -GameExe for read-only native proof.'}
foreach($testSource in @('native_effects_world_sprite_kl_test','native_effects_particle_normal_kl_test','native_projection_packet_tail_kl_test','native_secondary_mask_camera_production_test')) {
    $testExe=Join-Path $cache "$testSource.exe"
    [string[]]$proofExtra=@()
    if($testSource -in @('native_effects_world_sprite_kl_test','native_effects_particle_normal_kl_test','native_projection_packet_tail_kl_test')){$proofExtra+=$mh32}
    if($testSource -in @('native_effects_world_sprite_kl_test','native_effects_particle_normal_kl_test')){$proofExtra+='-ld3d9'}
    & $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude (TestSource $testSource) @proofExtra -o $testExe -lbcrypt
    if($LASTEXITCODE -ne 0){throw "Native depth/world-sprite proof compile failed: $testSource"}
    if($hasProofExe){& $testExe $GameExe;if($LASTEXITCODE -ne 0){throw "Native depth/world-sprite proof failed: $testSource"}}
    else{Write-Host "SKIP ${testSource}: supply -GameExe for pinned native proof."}
}
Write-Host '== HUD separation, objective presentation and tutorial ownership =='
foreach($testSource in @('native_postfilter_probe_kl_test','native_reflection_probe_kl_test','native_reflection_probe_cost_test','loading_memory_headroom_test','gpu_frame_texture_policy_test','native_reflection_packets_kl_test','native_reflection_camera_math_test','native_secondary_mask_projection_kl_test','native_objective_trace_kl_test','native_weapon_calibration_kl_test','native_blast_action_kl_test','native_marker_view_math_test','native_tutorial_kl_test','native_objective_visibility_kl_test','native_ledge_hint_kl_test','native_objective_goal_kl_test','native_passenger_pose_math_test','native_passenger_collision_kl_test','native_passenger_entry_kl_test','native_draw_scopes_test','native_skin_lighting_binding_test')) {
    foreach($architecture in @('x86','x64')) {
        $proofCompiler=if($architecture -eq 'x86'){$clang32}else{$clang64}
        $testExe=Join-Path $cache "$testSource-$architecture.exe"
        & $proofCompiler @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static (TestSource $testSource) -o $testExe
        if($LASTEXITCODE -ne 0){throw "HUD/tutorial policy compile failed: $testSource/$architecture"}
        if($hasProofExe){& $testExe $GameExe}else{& $testExe}
        if($LASTEXITCODE -ne 0){throw "HUD/tutorial policy failed: $testSource/$architecture"}
    }
}
foreach($testSource in @('native_tutorial_production_test','native_objective_visibility_production_test','native_shadow_head_production_test','native_ledge_hint_production_test','native_objective_goal_production_test','native_objective_marker_production_test','native_throwback_production_test','native_sprite_lifetime_production_test','native_gameplay_hud_production_test','native_hud_dependency_production_test','native_bones_light_cache_production_test','native_weapon_world_source_production_test','native_weapon_material_probe_production_test','native_shader_constant_map_test')) {
    $testExe=Join-Path $cache "$testSource.exe"
    & $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude (TestSource $testSource) -o $testExe -lbcrypt
    if($LASTEXITCODE -ne 0){throw "Private native HUD/tutorial/shadow compile failed: $testSource"}
    if($hasProofExe){& $testExe $GameExe;if($LASTEXITCODE -ne 0){throw "Private native HUD/tutorial/shadow failed: $testSource"}}
    else{Write-Host "SKIP ${testSource}: supply -GameExe for pinned native proof."}
}
foreach($testSource in @('native_legacy_hints_production_test','native_passenger_actor_production_test','native_passenger_collision_production_test')) {
    $testExe=Join-Path $cache "$testSource.exe"
    & $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude (TestSource $testSource) $mh32 -o $testExe -lbcrypt
    if($LASTEXITCODE -ne 0){throw "Private HUD/passenger adapter compile failed: $testSource"}
    if($hasProofExe){& $testExe $GameExe}else{& $testExe}
    if($LASTEXITCODE -ne 0){throw "Private HUD/passenger adapter failed: $testSource"}
}
Write-Host '== Actual native skin palette lifetime and read-only material evidence (NULLREF) =='
$skinLightingProof=Join-Path $cache 'native_skin_lighting_production_test.exe'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\native_skin_lighting_production_test.cpp" -o $skinLightingProof -ld3d9 -lgdi32 -luser32
if($LASTEXITCODE -ne 0){throw 'Native skin lighting ABI compile failed'}
& $skinLightingProof
if($LASTEXITCODE -ne 0){throw 'Native skin lighting CPU ABI failed'}
$skinPaletteProof=Join-Path $cache 'native_skin_palette_production_test.exe'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_skin_palette_production_test.cpp" -o $skinPaletteProof -lbcrypt -ld3d9 -luser32 -lgdi32
if($LASTEXITCODE -ne 0){throw 'Native palette lifetime/material compile failed'}
if($hasProofExe){& $skinPaletteProof $GameExe;if($LASTEXITCODE -ne 0){throw 'Native palette lifetime/material proof failed'}}
else{Write-Host 'SKIP native palette lifetime: supply -GameExe for pinned native proof.'}
foreach($testSource in @('native_accessory_parts_production_test','native_carried_gpu_production_test','native_authored_heading_gpu_production_test','native_passenger_roomscale_math_test','native_passenger_hands_production_test','native_passenger_head_production_test','vehicle_control_ipc_test','native_grenade_visual_kl_test')) {
    $testExe=Join-Path $cache "$testSource.exe"
    & $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude (TestSource $testSource) -o $testExe -lbcrypt
    if($LASTEXITCODE -ne 0){throw "Private adapter compile failed: $testSource"}
    & $testExe
    if($LASTEXITCODE -ne 0){throw "Private adapter failed: $testSource"}
}
$weaponFeedbackProof=Join-Path $cache 'native_weapon_feedback_production_test.exe'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_weapon_feedback_production_test.cpp" -o $weaponFeedbackProof -lbcrypt -ld3d9
if($LASTEXITCODE -ne 0){throw 'Weapon grip/stowed draw adapter compile failed'}
& $weaponFeedbackProof
if($LASTEXITCODE -ne 0){throw 'Weapon grip/stowed draw adapter failed'}
$tracerProof=Join-Path $cache 'native_tracer_visual_kl_test.exe'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_tracer_visual_kl_test.cpp" -o $tracerProof -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Native tracer camera producer compile failed'}
if($hasProofExe){& $tracerProof $GameExe;if($LASTEXITCODE -ne 0){throw 'Native tracer camera producer failed'}}
else{Write-Host 'SKIP native tracer camera producer: supply -GameExe for read-only native proof.'}
$markerWriterProof=Join-Path $cache 'native_marker_writer_production_test.exe'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_marker_writer_production_test.cpp" $mh32 -o $markerWriterProof -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Native marker writer precision compile failed'}
if($hasProofExe){& $markerWriterProof $GameExe;if($LASTEXITCODE -ne 0){throw 'Native marker writer precision failed'}}
else{Write-Host 'SKIP native marker writer precision: supply -GameExe for read-only native proof.'}
foreach($testSource in @('native_projection_tail_kl_test','native_mounted_aim_production_test','native_invulnerability_kl_test')) {
    $testExe=Join-Path $cache "$testSource.exe"
    & $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude (TestSource $testSource) $mh32 -o $testExe -lbcrypt
    if($LASTEXITCODE -ne 0){throw "Private MinHook adapter compile failed: $testSource"}
    if($hasProofExe){& $testExe $GameExe}else{& $testExe}
    if($LASTEXITCODE -ne 0){throw "Private MinHook adapter failed: $testSource"}
}
$actionProof=Join-Path $cache 'native_action_camera_production_test.exe'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_action_camera_production_test.cpp" -o $actionProof -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Actual action camera adapter compile failed'}
if($hasProofExe){& $actionProof $GameExe;if($LASTEXITCODE -ne 0){throw 'Actual action camera adapter failed'}}
else{Write-Host 'SKIP actual action camera adapter: supply -GameExe for read-only pinned executable proof.'}

Write-Host '== Cinema viewport ownership and restoration (NULLREF fixture) =='
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\native_cinema_viewport_test.cpp" -o "$cache\native_cinema_viewport_test.exe" -ld3d9 -luser32
if($LASTEXITCODE -ne 0){throw 'Cinema viewport proof compile failed'}
& "$cache\native_cinema_viewport_test.exe"
if($LASTEXITCODE -ne 0){throw 'Cinema viewport proof failed'}
Write-Host '== Retained performance, exception dump and process observer tests =='
Write-Host '== Weapon grip, collision preview and binocular aim geometry =='
foreach($testSource in @('native_two_hand_math_test','native_aim_kl_test','native_aim_render_math_test','native_reticle_kl_math_test')){
    $testExe=Join-Path $cache ($testSource+'.exe')
    & $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static (TestSource $testSource) -o $testExe
    if($LASTEXITCODE -ne 0){throw "Weapon test compile failed: $testSource"}
    & $testExe
    if($LASTEXITCODE -ne 0){throw "Weapon test failed: $testSource"}
}
Write-Host '== Verified static buffer batching and pre-camera diagnostics =='
foreach($testSource in @('native_reticle_style_kl_test','native_effects_particle_facing_kl_test','native_visual_trace_policy_test','native_laser_pose_kl_test')){
    foreach($architecture in @('x86','x64')) {
        $proofCompiler=if($architecture -eq 'x86'){$clang32}else{$clang64}
        $testExe=Join-Path $cache ($testSource+'-'+$architecture+'.exe')
        & $proofCompiler @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static (TestSource $testSource) -o $testExe -lbcrypt
        if($LASTEXITCODE -ne 0){throw "Render policy compile failed: $testSource/$architecture"}
        if($testSource -eq 'native_reticle_style_kl_test' -and $hasProofExe){& $testExe $GameExe}else{& $testExe}
        if($LASTEXITCODE -ne 0){throw "Render policy failed: $testSource/$architecture"}
    }
}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_buffer_flush_kl_test.cpp" "$root\src\render\native_buffer_flush_kl.cpp" $mh32 -o "$cache\native_buffer_flush_kl_test.exe" -lbcrypt -ld3d9 -ldxguid -lole32
if($LASTEXITCODE -ne 0){throw 'Static buffer test compile failed'}
if($GameExe){& "$cache\native_buffer_flush_kl_test.exe" $GameExe}else{& "$cache\native_buffer_flush_kl_test.exe"}
if($LASTEXITCODE -ne 0){throw 'Static buffer test failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\native_buffer_flush_sparse_test.cpp" -o "$cache\native_buffer_flush_sparse_test.exe"
if($LASTEXITCODE -ne 0){throw 'Sparse static upload policy test compile failed'}
& "$cache\native_buffer_flush_sparse_test.exe"
if($LASTEXITCODE -ne 0){throw 'Sparse static upload policy test failed'}
Write-Host '== Native wait import diagnostics and capture disposition =='
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\native_hid_refresh_kl_test.cpp" -o "$cache\native_hid_refresh_kl_test.exe"
if($LASTEXITCODE -ne 0){throw 'Native HID discovery test compile failed'}
& "$cache\native_hid_refresh_kl_test.exe"
if($LASTEXITCODE -ne 0){throw 'Native HID discovery test failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\native_frame_wait_kl_test.cpp" "$root\src\render\native_frame_wait_kl.cpp" -o "$cache\native_frame_wait_kl_test.exe"
if($LASTEXITCODE -ne 0){throw 'Native wait diagnostics test compile failed'}
& "$cache\native_frame_wait_kl_test.exe"
if($LASTEXITCODE -ne 0){throw 'Native wait diagnostics test failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\capture_disposition_integration_test.cpp" $mh32 -o "$cache\capture_disposition_integration_test.exe" -ld3d9 -lbcrypt -lgdi32 -luser32
if($LASTEXITCODE -ne 0){throw 'Production capture disposition test compile failed'}
& "$cache\capture_disposition_integration_test.exe"
if($LASTEXITCODE -ne 0){throw 'Production capture disposition test failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_effects_adapter_kl_test.cpp" $mh32 -o "$cache\native_effects_adapter_kl_test.exe" -ld3d9 -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Native world-effect mapped adapter test compile failed'}
& "$cache\native_effects_adapter_kl_test.exe"
if($LASTEXITCODE -ne 0){throw 'Native world-effect mapped adapter test failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_effects_geometry_kl_test.cpp" $mh32 -o "$cache\native_effects_geometry_kl_test.exe" -ld3d9 -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Native geometry billboard fixture compile failed'}
if ($hasProofExe) {
    & "$cache\native_effects_geometry_kl_test.exe" $GameExe
    if ($LASTEXITCODE) { throw 'Native geometry billboard fixture failed.' }
} else {
    Write-Host 'SKIP native geometry billboard: supply -GameExe for the native fixture.'
}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_postfilter_probe_production_test.cpp" $mh32 -o "$cache\native_postfilter_probe_production_test.exe" -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Native postfilter probe fixture compile failed'}
if($hasProofExe){& "$cache\native_postfilter_probe_production_test.exe" $GameExe;if($LASTEXITCODE -ne 0){throw 'Native postfilter probe fixture failed'}}
else{Write-Host 'SKIP postfilter probe: supply -GameExe for pinned native proof.'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_scope_fire_test.cpp" -o "$cache\native_scope_fire_test.exe" -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Native scoped fire callback fixture compile failed'}
& "$cache\native_scope_fire_test.exe"
if($LASTEXITCODE -ne 0){throw 'Native scoped fire callback fixture failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_two_hand_production_test.cpp" -o "$cache\native_two_hand_production_test.exe" -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Production foregrip rig fixture compile failed'}
& "$cache\native_two_hand_production_test.exe"
if($LASTEXITCODE -ne 0){throw 'Production foregrip rig fixture failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_stance_production_test.cpp" -o "$cache\native_stance_production_test.exe" -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Production stationary firing stance fixture compile failed'}
& "$cache\native_stance_production_test.exe"
if($LASTEXITCODE -ne 0){throw 'Production stationary firing stance fixture failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_stance_eye_production_test.cpp" -o "$cache\native_stance_eye_production_test.exe" -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Production stationary eye-height fixture compile failed'}
& "$cache\native_stance_eye_production_test.exe"
if($LASTEXITCODE -ne 0){throw 'Production stationary eye-height fixture failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_rappel_parent_production_test.cpp" -o "$cache\native_rappel_parent_production_test.exe" -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Production rappel anatomical parent fixture compile failed'}
& "$cache\native_rappel_parent_production_test.exe"
if($LASTEXITCODE -ne 0){throw 'Production rappel anatomical parent fixture failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_hip_attachment_production_test.cpp" -o "$cache\native_hip_attachment_production_test.exe" -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Production hip attachment fixture compile failed'}
& "$cache\native_hip_attachment_production_test.exe"
if($LASTEXITCODE -ne 0){throw 'Production hip attachment fixture failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_hip_rigid_production_test.cpp" -o "$cache\native_hip_rigid_production_test.exe" -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Production hip rigid fixture compile failed'}
& "$cache\native_hip_rigid_production_test.exe"
if($LASTEXITCODE -ne 0){throw 'Production hip rigid fixture failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_team_commands_production_test.cpp" -o "$cache\native_team_commands_production_test.exe" -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Production native squad command fixture compile failed'}
& "$cache\native_team_commands_production_test.exe"
if($LASTEXITCODE -ne 0){throw 'Production native squad command fixture failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\native_rope_kl_test.cpp" -o "$cache\native_rope_kl_test.exe" -lbcrypt
if($LASTEXITCODE -ne 0){throw 'Native owned rope visibility fixture compile failed'}
& "$cache\native_rope_kl_test.exe"
if($LASTEXITCODE -ne 0){throw 'Native owned rope visibility fixture failed'}
foreach($compiler in @($clang32,$clang64)){
    $bodyLifetime=Join-Path $cache (([IO.Path]::GetFileNameWithoutExtension($compiler))+'-body-lifetime.exe')
    & $compiler @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\native_body_lifetime_test.cpp" "$root\src\game\native_body_kl.cpp" -o $bodyLifetime
    if($LASTEXITCODE -ne 0){throw 'Production body lifetime test compile failed'}
    & $bodyLifetime
    if($LASTEXITCODE -ne 0){throw 'Production body lifetime test failed'}
}
foreach($test in @(
    @{Name='performance_log_test';Flags=@('-lbcrypt')},
    @{Name='crash_diagnostics_test';Flags=@('-municode')},
    @{Name='session_monitor_test';Flags=@('-municode','-lbcrypt','-luser32')}
)) {
    $testExe=Join-Path $cache ($test.Name+'.exe')
    & $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static (TestSource $($test.Name)) -o $testExe @($test.Flags)
    if($LASTEXITCODE -ne 0){throw "Diagnostic test compile failed: $($test.Name)"}
    & $testExe
    if($LASTEXITCODE -ne 0){throw "Diagnostic test failed: $($test.Name)"}
}
Test-PythonSuite 'test_analyze_kl1_performance.py' 'Performance analyzer tests failed'
Test-PythonSuite 'test_decode_visual_trace.py' 'Visual trace decoder tests failed'
Test-PythonSuite 'test_compare_kl1_performance.py' 'Performance comparison tests failed'

Write-Host '== Audio routing ownership and endpoint policy tests =='
Write-Host '== Stable native input policy and opt-in skin pass evidence =='
foreach($proofName in @('native_hands_native_input_pair_production_test','native_hands_skin_pass_pair_production_test')) {
    & $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude (TestSource $proofName) -o "$cache\$proofName.exe" -lbcrypt
    if($LASTEXITCODE -ne 0){throw "Production pair fixture compile failed: $proofName"}
    & "$cache\$proofName.exe"
    if($LASTEXITCODE -ne 0){throw "Production pair fixture failed: $proofName"}
}
Write-Host '== OpenXR GPU upload and persistent view resources (WARP) =='
$xrUploadTest=Join-Path $cache 'kl1_openxr_upload_test-x64.exe'
& $clang64 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\kl1_openxr_upload_test.cpp" -o $xrUploadTest -ld3d11 -ldxgi -luser32
if($LASTEXITCODE -ne 0){throw 'OpenXR upload/resource fixture compile failed'}
& $xrUploadTest
if($LASTEXITCODE -ne 0){throw 'OpenXR upload/resource fixture failed'}
& "$root\tests\tools\test-collect-diagnostics.ps1"
if(-not $?){throw 'Multi-session diagnostic collector tests failed'}
Write-Host '== Renderer selection encoding and GUI parity =='
$routeTest = Join-Path $cache 'launcher_startup_route_test.exe'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static (TestSource launcher_startup_route_test) -o $routeTest -lbcrypt
if ($LASTEXITCODE) { throw 'Launcher startup route test failed to compile.' }
& $routeTest
if ($LASTEXITCODE) { throw 'Launcher startup route test failed.' }
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -municode "$root\tests\native\game_renderer_selection_test.cpp" -o "$cache\game_renderer_selection_test.exe" -lbcrypt -lshell32 -lole32 -luuid
if($LASTEXITCODE -ne 0){throw 'Renderer selection test compile failed'}
& "$cache\game_renderer_selection_test.exe" $dist
if($LASTEXITCODE -ne 0){throw 'Renderer selection test failed'}
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\kl1_launcher_settings_test.cpp" -o "$cache\kl1_launcher_settings_test.exe" -lbcrypt -lshell32 -lole32 -luuid
if($LASTEXITCODE -ne 0){ throw 'Launcher settings test compile failed' }
& "$cache\kl1_launcher_settings_test.exe"
if($LASTEXITCODE -ne 0){ throw 'Launcher settings test failed' }
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\audio_route_test.cpp" "$root\src\audio\audio_route.cpp" -o "$cache\audio_route_test.exe" -ldsound -lole32 -luuid
if($LASTEXITCODE -ne 0){ throw 'Audio route test compile failed' }
& "$cache\audio_route_test.exe"
if($LASTEXITCODE -ne 0){ throw 'Audio route test failed' }
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static (TestSource audio_connection_selection_test) "$root\src\audio\audio_route.cpp" -o "$cache\audio_connection_selection_test.exe" -ldsound -lole32 -luuid
if ($LASTEXITCODE) { throw 'Audio connection selection test failed to compile.' }
& "$cache\audio_connection_selection_test.exe"
if ($LASTEXITCODE) { throw 'Audio connection selection test failed.' }
Write-Host 'D3D9 hands rendering integration test (run separately with the game closed):'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\hands_renderer_test.cpp" "$root\src\render\hands_renderer.cpp" -o "$cache\hands_renderer_test.exe" -ld3d9 -luser32 -lgdi32
if($LASTEXITCODE -ne 0){ throw 'Hands renderer integration test compile failed' }
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -I $mhInclude "$root\tests\native\native_marker_render_kl_test.cpp" $mh32 -o "$cache\native_marker_render_kl_test.exe" -ld3d9 -lbcrypt -lgdi32 -luser32
if($LASTEXITCODE -ne 0){throw 'Native marker production HAL test compile failed'}
Write-Host 'Native marker Position/Emit/Draw/Indexed HAL fixture compiled; run separately with the supported EXE for opcode proof.'
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static "$root\tests\native\d3d9_normalization_test.cpp" -o "$cache\d3d9_normalization_test.exe" -ld3d9 -luser32
if($LASTEXITCODE -ne 0){ throw 'D3D9 normalization integration test compile failed' }
Write-Host '== Early audio loader safety test (no game or audio stream) =='
& $clang32 @includeFlags -std=c++17 -O2 -Wall -Wextra -Werror -static -municode "$root\tests\fixtures\audio-bootstrap-safety-test.cpp" -o "$cache\audio-bootstrap-safety-test.exe"
if($LASTEXITCODE -ne 0){ throw 'Audio loader safety test compile failed' }
& "$cache\audio-bootstrap-safety-test.exe" "$dist\KaneLynchVRAudio.dll"
if($LASTEXITCODE -ne 0){ throw 'Audio loader safety test failed' }
Write-Host '== Cross-architecture real GPU/CPU transport and Reset matrix =='
& "$root\tests\tools\test-gpu-transport.ps1" -ToolchainBin $bin -OculusHeaders $ovrHeaders -OutputDirectory (Join-Path $cache 'gpu-transport')
if(-not $?){throw 'Cross-architecture GPU/CPU transport checks failed'}
Write-Host "Host --check skipped by default (game may be in use); use KaneLynchVRHost.exe --check manually."
