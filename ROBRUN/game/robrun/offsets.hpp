#pragma once
#include <cinttypes>
#include <cstdint>

// =============================================================================
// ROB RUN - Unity 6.3.18f1 (6000.3.18f1, 5ebeb53e4c07)
// IL2CPP metadata v39, EAC protected.
//
// Source: Il2CppDumper-unity6 output for the local Steam build
//   dump.cs / il2cpp.h / script.json
// Regenerate the bulk data with tools/il2cpp_convert.py (generated/offsets.hpp)
// and tools/gen_holder_graph.py (generated/holder_graph.hpp).
//
// Everything in this file was read out of the dump and cross-checked against
// the matching il2cpp.h struct body. Field offsets are object-relative, i.e.
// 0x10 is the first managed field (Il2CppObject header is 0x10 bytes).
//
// The game is ECS: gameplay state lives in plain managed model objects, not in
// MonoBehaviours. The only scene objects we touch are Cinemachine (camera).
// =============================================================================

namespace robrun
{
    using uptr = std::uintptr_t;

    // -------------------------------------------------------------------------
    // Unity IL2CPP runtime layout (metadata v39 / Unity 6).
    // Verify with tools/struct_layout.py --h il2cpp.h Il2CppClass_1 Il2CppClass_2
    // -------------------------------------------------------------------------
    namespace il2cpp
    {
        constexpr uptr Object_Klass              = 0x00;
        constexpr uptr Object_Monitor            = 0x08;

        constexpr uptr Class_Name                = 0x10; // const char*
        constexpr uptr Class_Namespace           = 0x18; // const char*
        constexpr uptr Class_ElementClass        = 0x40; // non-null for arrays
        constexpr uptr Class_DeclaringType       = 0x50;
        constexpr uptr Class_Parent              = 0x58;
        constexpr uptr Class_StaticFields        = 0xB8; // void*
        constexpr uptr Class_RgctxData           = 0xC0;
        constexpr uptr Class_2                   = 0xC8;
        constexpr uptr Class_InstanceSize        = 0xF8; // _2 + 0x30, uint32
        constexpr uptr Class_ElementSize         = 0x100; // _2 + 0x38, uint32
        constexpr uptr Class_StaticFieldsSize    = 0x108; // _2 + 0x40, uint32

        // System.Object[] / List<T> / Dictionary<TKey,TValue>
        constexpr uptr Array_Length              = 0x18;
        constexpr uptr Array_Data                = 0x20;
        constexpr uptr List_Items                = 0x10;
        constexpr uptr List_Size                 = 0x18;
        constexpr uptr Dictionary_Entries        = 0x18;
        constexpr uptr Dictionary_Count          = 0x20;
        constexpr uptr Dictionary_Keys           = 0x38;
        constexpr uptr Dictionary_Values         = 0x40;
        constexpr uptr Dictionary_EntryStride    = 0x18;
        constexpr uptr Dictionary_EntryHashCode  = 0x00;
        constexpr uptr Dictionary_EntryKey       = 0x08;
        constexpr uptr Dictionary_EntryValue     = 0x10;

        // System.String (UTF-16)
        constexpr uptr String_Length             = 0x10;
        constexpr uptr String_Chars              = 0x14;

        // System.Delegate / MulticastDelegate
        constexpr uptr Delegate_Target           = 0x20;
        constexpr uptr MulticastDelegate_List    = 0x78;

        // Framework.Replication.Data.Primitive.BasePrimitiveData<T>
        //   _serializer @ 0x10, _value @ 0x18. For value types (float / int /
        //   bool / Vector3) _value is stored INLINE at 0x18 (verified with a
        //   live read: Vector3 x/y/z at 0x18/0x1C/0x20). For reference types it
        //   is the object pointer at 0x18.
        constexpr uptr PrimitiveData_Value       = 0x18; // address of _value
        constexpr uptr PrimitiveData_Default     = 0x28;

        // Models.Models.PrimitiveModel.NoReplicationPrimitiveModel<T>
        //   boxed/reference value directly here.
        constexpr uptr PrimitiveModel_Value       = 0x10;
        // Models.Models.PrimitiveModel.PrimitiveModel<T> wraps IPrimitiveData<T>.
        constexpr uptr PrimitiveModel_Data        = 0x10;
    }

    // -------------------------------------------------------------------------
    // TypeInfo slots (script.json ScriptMetadata[].Address, RVA from GameAssembly).
    //   klass = read_ptr(game_assembly_base + rva)
    // TypeDefIndex is the dump.cs index, used by generated/type_indices.hpp.
    // -------------------------------------------------------------------------
    namespace rva
    {
        // --- goals -----------------------------------------------------------
        constexpr uptr WorldEntitiesModel_TypeInfo            = 0x819B280; // TypeDef 3335
        constexpr uptr BattleEntitiesModel_TypeInfo           = 0x81BCF30; // TypeDef 5510
        constexpr uptr BattleModel_TypeInfo                   = 0x81C0888; // TypeDef 5520
        constexpr uptr BattleEntitiesUpdater_TypeInfo         = 0x81BD3F8; // TypeDef 5517

        // --- carriers --------------------------------------------------------
        constexpr uptr BattleScreenUpdater_TypeInfo           = 0x81C8CC8; // TypeDef 5538
        constexpr uptr BattleScreenFactory_TypeInfo           = 0x81C8998; // TypeDef 5536
        constexpr uptr Game_Game_TypeInfo                     = 0x812DE18; // TypeDef 29330
        constexpr uptr GlobalDebugUpdater_TypeInfo            = 0x8135930; // TypeDef 10733
        constexpr uptr BattleCharacterModel_TypeInfo          = 0x81B7928; // TypeDef 4309
        constexpr uptr PlayerBattleModel_TypeInfo             = 0x81F0148; // TypeDef 3695
        constexpr uptr BattleCharacterEntityModel_TypeInfo    = 0x81B6608; // TypeDef 9036
        constexpr uptr PlayerEntityModel_TypeInfo             = 0x81F3238; // TypeDef 8692
        constexpr uptr BattleCameraModel_TypeInfo             = 0x81B3C98; // TypeDef 7868

        // --- model internals -------------------------------------------------
        constexpr uptr HealthModel_TypeInfo                   = 0x814AC68; // TypeDef 3919
        constexpr uptr SkeletonModel_TypeInfo                 = 0x81B9B28; // TypeDef 3485
        constexpr uptr TransformModel_TypeInfo                = 0x812A878; // TypeDef 3428
        constexpr uptr CharacterReplicatedTransform_TypeInfo  = 0x813A128; // TypeDef 4097
        constexpr uptr BattleCharacterData_TypeInfo           = 0x81B62D8; // TypeDef 4889
        constexpr uptr BattleCharacterCommonData_TypeInfo     = 0x81B57B0; // TypeDef 4888
        constexpr uptr BattleCharacterPrivateData_TypeInfo    = 0x81B7F80; // TypeDef 4891
        constexpr uptr BattleCharacterPrivateModel_TypeInfo   = 0x81B8118; // TypeDef 4298
        constexpr uptr PositionModel_TypeInfo                  = 0x8109428; // TypeDef 3626
        constexpr uptr CharacterCenterPositionModel_TypeInfo  = 0x8127BB8; // TypeDef 4111
        constexpr uptr PlayerBattleData_TypeInfo               = 0x81EFFB8; // TypeDef 4989

        // --- statics / roots -------------------------------------------------
        constexpr uptr SoundEventBus_TypeInfo                 = 0x81CCD18; // TypeDef 35341
        constexpr uptr ContentContext_TypeInfo                = 0x818D2D8; // TypeDef 36302
        constexpr uptr DebugUtils_TypeInfo                    = 0x81C93B0; // TypeDef 5773
        constexpr uptr Application_TypeInfo                   = 0x8195CF0; // TypeDef 16971
        constexpr uptr SceneManager_TypeInfo                  = 0x8184B68; // TypeDef 17458
        constexpr uptr Debug_TypeInfo                          = 0x81C4F28; // TypeDef 17008
        constexpr uptr PlayerInputManager_TypeInfo            = 0x81F5CC8; // TypeDef 21134
        constexpr uptr CinemachineCore_TypeInfo               = 0x814AD78; // TypeDef 29610
        constexpr uptr CinemachineBrain_TypeInfo               = 0x814AA48; // TypeDef 29513
        constexpr uptr MulticastDelegate_TypeInfo              = 0x8174FC0;
        constexpr uptr Delegate_TypeInfo                       = 0x81D7790;

        // --- descriptions (static instances -> pointer -> name) -------------
        constexpr uptr PlayerRoleDescriptions_TypeInfo         = 0x81F9598; // TypeDef 1217
        constexpr uptr PlayerRoleDescription_TypeInfo           = 0x81F9408; // TypeDef 2880
        constexpr uptr BattleStanceDescription_TypeInfo        = 0x81C9CB8;
        // PhysicsCharacterConfigs.Default static instance (body size fallback).
        constexpr uptr PhysicsCharacterConfigs_TypeInfo        = 0x81DD9E0;
    }

    // -------------------------------------------------------------------------
    // Gameplay model offsets.
    // -------------------------------------------------------------------------
    namespace off
    {
        namespace EntityModel // Game.Battle.EntityModel.EntityModel
        {
            constexpr uptr Id                    = 0x10; // int
        }

        namespace PlayerEntityModel
        {
            constexpr uptr ClientModel           = 0x18;
            constexpr uptr ServerModel           = 0x20; // BattleCharacterModel*
            constexpr uptr PublicId              = 0x28; // long
        }

        namespace BattleCharacterEntityModel
        {
            constexpr uptr Slots                 = 0x18;
            constexpr uptr ClientModel           = 0x20;
            constexpr uptr ServerModel           = 0x28; // BattleCharacterModel*
            constexpr uptr SimulatedModel        = 0x30; // BattleCharacterModel* (client predicted / animated)
        }

        namespace BattleEntitiesModel
        {
            constexpr uptr PhysicsSceneModel     = 0x10;
            constexpr uptr PlayerBattleModel     = 0x18;
            constexpr uptr InventoryModels       = 0x20;
            constexpr uptr PhysicsBodiesModel    = 0x40;
            constexpr uptr Models                = 0x50; // Dictionary<int, IEntityModel>
            constexpr uptr PlotsModels           = 0x58;
            constexpr uptr WorldEntitiesModel    = 0x60;
            constexpr uptr BattleModel           = 0x90;
            constexpr uptr Player                = 0xD0; // PlayerEntityModel*
            constexpr uptr SelfPlot              = 0xD8;
            constexpr uptr EntityModels          = 0xE0;

            // static pools (only used to prove static access works)
            constexpr uptr s_GrenadeSnapshots    = 0x00;
            constexpr uptr s_GrenadeExtrapolations = 0x08;
        }

        namespace BattleModel
        {
            constexpr uptr BattleEntitiesModel   = 0x90;
            constexpr uptr CharacterSlotsModel   = 0x248;
            constexpr uptr BattleCameraModel     = 0x358;
            constexpr uptr PlayerBattleModel     = 0x498;
            constexpr uptr WorldEntitiesModel    = 0x510;
        }

        namespace BattleEntitiesUpdater
        {
            constexpr uptr BattleEntitiesModel   = 0x18;
            constexpr uptr UpdatedEntitiesModel  = 0x20;
            constexpr uptr PlayerEntityId        = 0x28; // int
            constexpr uptr EntitiesModelsUpdaters = 0x30;
            constexpr uptr EntitiesViewsUpdaters  = 0x38;
            constexpr uptr BattleScreenContent    = 0x68;
            constexpr uptr BattleModel            = 0x70;
            constexpr uptr GlobalModel            = 0x78;
            constexpr uptr CharacterEntityViews   = 0x110;
        }

        namespace GlobalModel
        {
            constexpr uptr Party                  = 0xB8; // PartyModel
        }

        namespace PartyModel
        {
            constexpr uptr Entities               = 0x10; // HashSet<int> entity ids
        }

        namespace RobberInventoryCostModel
        {
            constexpr uptr CharacterInventory     = 0x10; // int, carried loot cost
        }

        namespace LootNodeModel
        {
            constexpr uptr Description            = 0x10; // LocationLootNodeDescription
            constexpr uptr UnlockState            = 0x20; // IPrimitiveModel<UnlockStateDescription>
            constexpr uptr IsEmpty                = 0x30; // IPrimitiveModel<bool>
        }

        namespace LocationLootNodeDescription
        {
            constexpr uptr Position               = 0x38; // Vector3
        }

        namespace CashLootNodeModel
        {
            constexpr uptr CashAmount             = 0x10; // IPrimitiveData<int>
            constexpr uptr Description            = 0x18; // LocationCashLootNodeDescription
        }

        namespace LocationCashLootNodeDescription
        {
            constexpr uptr Position               = 0x24; // Vector3
        }

        namespace SafeDoorModel
        {
            constexpr uptr IsUnlocked             = 0x10; // IPrimitiveData<bool>
            constexpr uptr Description            = 0x28; // SafeDoorDescription
        }

        namespace SafeDoorDescription
        {
            constexpr uptr Position               = 0x14; // Vector3
        }

        namespace HideObjectModel
        {
            constexpr uptr EntityId               = 0x10; // int, -1 when empty
            constexpr uptr Description            = 0x18; // LocationHideObjectDescription
        }

        namespace LocationHideObjectDescription
        {
            constexpr uptr Position               = 0x30; // Vector3
        }

        namespace ShelvingModel
        {
            constexpr uptr DescriptionModel       = 0x18; // DescriptionModel<LocationShelvingDescription>
        }

        namespace LocationShelvingDescription
        {
            constexpr uptr ShelvingDescription    = 0x18;
            constexpr uptr Position               = 0x20; // Vector3
        }

        namespace VehicleModel
        {
            constexpr uptr Transform              = 0x28; // VehicleTransformModel
            constexpr uptr OwnerRole              = 0x50; // IPrimitiveModel<PlayerRoleDescription>
            constexpr uptr LinearVelocity         = 0x60; // VelocityModel
        }

        namespace CarModel
        {
            constexpr uptr OwnerRole              = 0x48;
            constexpr uptr Description            = 0x78;
            constexpr uptr Transform              = 0xC0;
            constexpr uptr LinearVelocity         = 0xC8;
            constexpr uptr Siren                  = 0x1D0; // CarSirenModel
        }

        namespace HelicopterModel
        {
            constexpr uptr Description            = 0x48;
            constexpr uptr Transform              = 0x70;
            constexpr uptr LinearVelocity         = 0x78;
        }

        namespace VehicleTransformModel
        {
            constexpr uptr Position               = 0x10; // IPrimitiveData<Vector3>
            constexpr uptr PivotPosition          = 0x54; // Vector3, live pivot
        }

        namespace VelocityModel
        {
            constexpr uptr Data                   = 0x10; // IPrimitiveData<Vector3>
        }

        namespace CarSirenModel
        {
            constexpr uptr IsEnabled              = 0x10; // IPrimitiveData<bool>
        }

        // Carriers found in dump.cs but not needed by the current search.
        // Kept here because a future resolver root may start from one of them
        // (UnityEntry -> Game -> ScreenManager -> BattleUpdater chain).
        namespace BattleScreenFactory
        {
            constexpr uptr BattleModel           = 0x28;  // BattleModel*
            constexpr uptr BattleEntitiesModel   = 0x88;  // IBattleEntitiesModel*
        }

        namespace BattleUpdater
        {
            constexpr uptr BattleModel           = 0xE0;
            constexpr uptr BattleScreenFactory   = 0x180;
        }

        namespace ScreenManager
        {
            constexpr uptr CurrentUpdater        = 0x50;  // IUpdater*
        }

        namespace Game
        {
            constexpr uptr Updaters              = 0x20;  // List<IUpdater>
            constexpr uptr StartGameProcessor    = 0x98;  // was 0x90 before 2026-11-07 update
        }

        namespace GlobalDebugUpdater
        {
            constexpr uptr MainUpdaters          = 0x10;  // List<IUpdater>
        }

        namespace UnityEntry
        {
            constexpr uptr Game                  = 0x28;  // Game*
        }

        namespace WorldEntitiesModel
        {
            constexpr uptr NotSpawnedBattleCharacters = 0x10; // Dictionary<int, BattleCharacterModel>
            constexpr uptr BattleCharacters           = 0x18; // Dictionary<int, BattleCharacterModel>
            constexpr uptr Containers                 = 0x20;
            constexpr uptr Loots                      = 0x38;
            constexpr uptr Vehicles                   = 0x58;
            constexpr uptr Cars                       = 0x60;
            constexpr uptr Helicopters                = 0x68;
            constexpr uptr Grenades                   = 0x1A0;
            constexpr uptr PlayerObservers            = 0x1C0;
            constexpr uptr Bots                       = 0x318;
            constexpr uptr LootNodes                  = 0xB0;
            constexpr uptr SafeDoors                  = 0x110;
            constexpr uptr Shelvings                  = 0x148;
            constexpr uptr HideObjects                = 0x1B0;
            constexpr uptr CashLootNodes              = 0x268;
            constexpr uptr CopRewardBoxes             = 0x2B8;
        }

        namespace BattleCharacterModel
        {
            constexpr uptr PrivateModel          = 0x10;
            constexpr uptr Id                    = 0x38; // int
            constexpr uptr Data                  = 0x40; // BattleCharacterData*
            constexpr uptr Config                = 0x48;
            constexpr uptr Transform             = 0x58; // ITransformModel
            constexpr uptr ReplicatedTransform   = 0x60;
            constexpr uptr Velocity              = 0xB8;
            constexpr uptr GroundModel           = 0xD0; // CharacterControllerGroundModel
            constexpr uptr IsDead                = 0xF0; // IPrimitiveModel<bool>
            constexpr uptr HitBoxAccess          = 0x120;
            constexpr uptr SkeletonAnimationModel = 0x130;
            constexpr uptr SkeletonModel         = 0x138;
            constexpr uptr Animator              = 0x140;
            constexpr uptr IsSprintingModel      = 0x148;
            constexpr uptr Weapon                = 0x150;
            constexpr uptr HasArmor              = 0x1B8; // IPrimitiveModel<bool>
            constexpr uptr BattleStanceModel     = 0x1F0;
            constexpr uptr ArmedModel            = 0x1F8;
            constexpr uptr Role                  = 0x208; // IPrimitiveModel<PlayerRoleDescription>
            constexpr uptr ArrestTargetModel     = 0x250;
            constexpr uptr PlayerBattleModel     = 0x258;
            constexpr uptr RobberInventoryCost   = 0x268; // RobberInventoryCostModel
            constexpr uptr CenterPosition        = 0x290; // IPositionModel
            constexpr uptr KnockoutModel         = 0x298; // Models.Models.Knockout.KnockoutModel
            constexpr uptr SurrenderStateModel   = 0x310;
            constexpr uptr CapsuleColliderParams = 0x318; // ICapsuleColliderParamsModel
        }

        namespace KnockoutModel
        {
            constexpr uptr EndTs                 = 0x10; // IPrimitiveData<long>
            constexpr uptr KnockoutPhase         = 0x18; // IPrimitiveData<KnockoutPhaseDescription>
            constexpr uptr IsSurrendered         = 0x70; // bool
            constexpr uptr IsHandcuffing         = 0x71; // bool
            constexpr uptr IsHandcuffed          = 0x72; // bool
        }

        namespace ArrestTargetModel
        {
            // Best-effort: layout varies; read conservatively in code.
            constexpr uptr TargetEntityId        = 0x10;
        }

        // Models.Models.CapsuleColliderParamsModel
        namespace CapsuleColliderParamsModel // NotReplicatedCapsuleColliderParamsModel
        {
            constexpr uptr Radius                = 0x10; // float
            constexpr uptr Length                = 0x14; // float
            constexpr uptr DistanceToGround      = 0x18; // float
        }

        namespace ReplicatedCapsuleColliderParamsModel
        {
            constexpr uptr Radius                = 0x10; // IPrimitiveData<float>
            constexpr uptr Length                = 0x18; // IPrimitiveData<float>
            constexpr uptr DistanceToGround      = 0x20; // IPrimitiveData<float>
        }

        // Models.Models.CharacterController.CharacterControllerGroundModel
        // Plain managed fields, updated locally by the controller system.
        namespace CharacterControllerGroundModel
        {
            constexpr uptr GroundSurfaceType     = 0x10; // IPrimitiveData<byte>
            constexpr uptr GroundFound           = 0x18; // bool
            constexpr uptr GroundPoint           = 0x1C; // Vector3
            constexpr uptr GroundNormal          = 0x28; // Vector3
        }

        namespace BattleCharacterConfig
        {
            constexpr uptr PhysicsCharacter      = 0x18; // PhysicsCharacterConfig*
        }

        namespace PhysicsCharacterConfig
        {
            constexpr uptr DefaultRadius         = 0x18; // float
            constexpr uptr StandLength           = 0x1C; // float
            constexpr uptr CrouchLength          = 0x20; // float
        }

        namespace BattleCharacterPrivateModel
        {
            constexpr uptr Inventory             = 0x150;
            constexpr uptr EquipSlots            = 0x158;
            constexpr uptr Pockets               = 0x170;
            constexpr uptr Armor                 = 0x178;
            constexpr uptr Health                = 0x1B0; // HealthModel*
        }

        namespace BattleCharacterData // CompositeData
        {
            constexpr uptr CommonData            = 0x20;
            constexpr uptr PrivateData           = 0x28;
            constexpr uptr PublicData            = 0x30;
        }

        namespace BattleCharacterCommonData
        {
            constexpr uptr Role                  = 0x20; // IPrimitiveData<PlayerRoleDescription>
            constexpr uptr Position              = 0x28; // IPrimitiveData<Vector3>
            constexpr uptr LookDirection         = 0x30;
            constexpr uptr Rx                    = 0x38;
            constexpr uptr Ry                    = 0x40;
            constexpr uptr Rz                    = 0x48;
            constexpr uptr IsSprinting           = 0x88;
            constexpr uptr IsCrouching           = 0xA8;
            constexpr uptr ItemInHands           = 0xB8;
            constexpr uptr IsDead                = 0xE0;
            constexpr uptr BattleStance          = 0x100;
            constexpr uptr IsArmed               = 0x108;
        }

        namespace BattleCharacterPrivateData
        {
            constexpr uptr Health                = 0x38; // IPrimitiveData<int>
        }

        namespace HealthModel
        {
            constexpr uptr Health                = 0x10; // IPrimitiveData<int>
            constexpr uptr IsDead                = 0x18; // IPrimitiveModel<bool>
            constexpr uptr MaxAmount             = 0x20; // int
            constexpr uptr CriticalHealth        = 0x24; // int
        }

        namespace PositionModel
        {
            constexpr uptr X                     = 0x10; // float
            constexpr uptr Y                     = 0x14;
            constexpr uptr Z                     = 0x18;
        }

        namespace CharacterCenterPositionModel
        {
            constexpr uptr PositionModel         = 0x10;
            constexpr uptr CrouchModel           = 0x18;
            constexpr uptr PhysicsCharacterConfig = 0x20;
            constexpr uptr KnockoutModel         = 0x28;
        }

        namespace CharacterReplicatedTransformModel
        {
            constexpr uptr Position              = 0x10; // IPrimitiveData<Vector3>
            constexpr uptr Rx                    = 0x18;
            constexpr uptr Ry                    = 0x20;
            constexpr uptr Rz                    = 0x28;
        }

        namespace TransformModel
        {
            constexpr uptr X                     = 0x10; // IPrimitiveData<float>
            constexpr uptr Y                     = 0x18;
            constexpr uptr Z                     = 0x20;
            constexpr uptr Rx                    = 0x28;
            constexpr uptr Ry                    = 0x30;
            constexpr uptr Rz                    = 0x38;
        }

        namespace SkeletonModel
        {
            constexpr uptr SkeletonDescription   = 0x10;
            constexpr uptr BonesData             = 0x18; // float[]
        }

        // ---- Unity visual rig (skeleton ESP) --------------------------------
        // The gameplay SkeletonModel is never populated in this build (the
        // HitBoxesUpdater that calls SetSkeleton is gated behind a disabled
        // battle mode), so the skeleton is read from the Unity transforms of
        // the character's view instead.
        namespace BattleCharacterEntityView
        {
            constexpr uptr MovementView          = 0x20; // CharacterMovementView*
            constexpr uptr AnimationView         = 0x28; // CharacterAnimationView*
            constexpr uptr WithWeaponAnimationView = 0x30; // CharacterWithWeaponAnimationView*
            constexpr uptr HeadPosition          = 0xE8; // Transform
            constexpr uptr SpineAdaptation       = 0xF8; // SpineAdaptationManualBehavior*
            constexpr uptr NeckAdaptation        = 0x100; // HeadAdaptationManualBehavior*
            constexpr uptr HeadAdaptation        = 0x108; // HeadAdaptationManualBehavior*
        }

        namespace SpineAdaptationManualBehavior
        {
            constexpr uptr Root                  = 0x20; // Transform
            constexpr uptr BoneTarget            = 0x28; // Transform
            constexpr uptr BoneEdge              = 0x30; // Transform
        }

        namespace HeadAdaptationManualBehavior
        {
            constexpr uptr Root                  = 0x20; // Transform
            constexpr uptr BoneTarget            = 0x28; // Transform
        }

        namespace CharacterMovementView
        {
            constexpr uptr Transform             = 0x20; // UnityEngine.Transform
        }

        namespace CharacterAnimationView
        {
            constexpr uptr Animator              = 0x20;
            constexpr uptr HeadManualControl     = 0x28; // CharacterHeadManualControlView*
            constexpr uptr TorsoDeformation      = 0x30; // CharacterTorsoDeformationView*
        }

        namespace CharacterHeadManualControlView
        {
            constexpr uptr Neck                  = 0x20; // Transform
            constexpr uptr Head                  = 0x28; // Transform
            constexpr uptr SpineTop              = 0x30; // Transform
        }

        namespace CharacterTorsoDeformationView
        {
            constexpr uptr Root                  = 0x20; // Transform (hips)
            constexpr uptr Spine2                = 0x28; // Transform
            constexpr uptr Spine3                = 0x30; // Transform
        }

        namespace CharacterWithWeaponAnimationView
        {
            constexpr uptr ArmedPose             = 0x30; // PoseIKView*
            constexpr uptr LeftElbowPose         = 0x38; // PoseIKView*
            constexpr uptr LeftWristPose         = 0x40; // PoseIKView*
            constexpr uptr RightWristPose        = 0x48; // PoseIKView*
            constexpr uptr LeftHandIK            = 0x58; // LimbIKView*
            constexpr uptr RightHandIK           = 0x60; // LimbIKView*
            constexpr uptr LeftFootIK            = 0x68; // LimbIKView*
            constexpr uptr RightFootIK           = 0x70; // LimbIKView*
            constexpr uptr NeckIK                = 0x78; // LimbIKView*
        }

        namespace PoseIKView
        {
            constexpr uptr Bones                 = 0x30; // Transform[]
        }

        namespace LimbIKView
        {
            constexpr uptr Bones                 = 0x40; // Transform[]
        }

        // Models.Models.Skeleton.SkeletonConfig
        namespace SkeletonConfig
        {
            constexpr uptr BonesCount                 = 0x10; // int
            constexpr uptr BoneDataSize               = 0x14; // int (in float elements)
            constexpr uptr LocalPosOriOffset          = 0x18; // int (float element offset)
            constexpr uptr ModelOriOffset             = 0x1C; // int
            constexpr uptr LengthOffset               = 0x20; // int
            constexpr uptr HierarchyData              = 0x28; // int[]
            constexpr uptr MaxChildrenCount           = 0x30; // int
            constexpr uptr RootIndex                  = 0x34; // int
        }

        namespace PlayerBattleModel
        {
            constexpr uptr Data                  = 0x10; // PlayerBattleData*
        }

        namespace PlayerBattleData
        {
            constexpr uptr CopName               = 0x30; // IPrimitiveData<string>
            constexpr uptr CopPublicId           = 0x40;
            constexpr uptr KillerName            = 0xF0;
            constexpr uptr KillerPublicId        = 0xF8;
        }

        // Game.Battle.Entities.Player.Camera.BattleCameraModel
        namespace BattleCameraModel
        {
            constexpr uptr Position              = 0x10; // Vector3
            constexpr uptr Orientation           = 0x1C; // Quaternion
            constexpr uptr LookAtOrientation     = 0x2C;
            constexpr uptr Forward               = 0x3C;
            constexpr uptr Right                 = 0x48;
            constexpr uptr Up                    = 0x54;
            constexpr uptr DistanceToTarget      = 0x60; // float
        }

        // Identified / EnumItem (role & stance description ids)
        namespace Identified
        {
            constexpr uptr Id                    = 0x10; // string
        }
        namespace EnumItem
        {
            constexpr uptr Index                 = 0x18; // int
        }
    }

    // -------------------------------------------------------------------------
    // Static singleton / delegate slots (offset inside the static_fields block).
    // -------------------------------------------------------------------------
    namespace statics
    {
        namespace SoundEventBus // Core.EventBus.SoundEventBus
        {
            constexpr uptr Instance              = 0x00;
            constexpr uptr Subscribers           = 0x10; // Dictionary<Type, List<Delegate>>
        }

        namespace ContentContext // Content.ContentContext
        {
            constexpr uptr Instance              = 0x00;
        }

        namespace DebugUtils // Game.Battle.Utils.DebugUtils
        {
            constexpr uptr Gizmos                = 0x00; // SortedSet<ValueTuple<long,long,Action>>
            constexpr uptr Now                   = 0x08;
        }

        namespace Application // UnityEngine.Application
        {
            constexpr uptr LowMemory             = 0x00;
            constexpr uptr MemoryUsageChanged    = 0x08;
            constexpr uptr LogCallback           = 0x10;
            constexpr uptr LogCallbackThreaded   = 0x18;
            constexpr uptr FocusChanged          = 0x20;
            constexpr uptr DeepLinkActivated     = 0x28;
            constexpr uptr WantsToQuit           = 0x30;
            constexpr uptr Quitting              = 0x38;
            constexpr uptr Unloading             = 0x40;
        }

        namespace SceneManager // UnityEngine.SceneManagement.SceneManager
        {
            constexpr uptr SceneLoaded           = 0x08;
            constexpr uptr SceneUnloaded         = 0x10;
            constexpr uptr ActiveSceneChanged    = 0x18;
        }

        namespace Debug_ // UnityEngine.Debug
        {
            constexpr uptr DefaultLogger         = 0x00;
            constexpr uptr Logger                = 0x08;
        }

        namespace PlayerInputManager
        {
            constexpr uptr Instance              = 0x00;
        }

        namespace GlobalDebugUpdater // Game.Global.GlobalDebug.GlobalDebugUpdater
        {
            constexpr uptr Instance              = 0x00;
        }

        namespace CinemachineCore
        {
            constexpr uptr SInstance             = 0x08;
        }

        namespace PlayerRoleDescriptions
        {
            constexpr uptr Settler               = 0x08;
            constexpr uptr Citizen               = 0x10;
            constexpr uptr Robber                = 0x18;
            constexpr uptr Cop                   = 0x20;
            constexpr uptr ClanBattleMember      = 0x28;
            constexpr uptr Prisoner              = 0x30;
            constexpr uptr Hubber                = 0x38;
        }

        namespace BattleStanceDescription
        {
            constexpr uptr Disabled              = 0x08;
            constexpr uptr Weapon                = 0x10;
            constexpr uptr Boxing                = 0x18;
        }
    }

    // -------------------------------------------------------------------------
    // Cinemachine camera (all managed - no native Camera reads).
    // -------------------------------------------------------------------------
    namespace cam
    {
        namespace CoreObject // CinemachineCore instance
        {
            constexpr uptr ActiveBrains          = 0x10; // List<CinemachineBrain>
            constexpr uptr ActiveCameras         = 0x18; // List<CinemachineVirtualCameraBase>
            constexpr uptr AllCameras            = 0x28;
        }

        namespace Brain
        {
            constexpr uptr OutputCamera          = 0x50;
            constexpr uptr CurrentCameraState    = 0xB0; // CameraState (inline struct)
        }

        namespace State // Cinemachine.CameraState, relative to Brain + CurrentCameraState
        {
            constexpr uptr Lens                  = 0x00;
            constexpr uptr ReferenceUp           = 0x34;
            constexpr uptr ReferenceLookAt       = 0x40;
            constexpr uptr RawPosition           = 0x4C;
            constexpr uptr RawOrientation        = 0x58;
            constexpr uptr PositionCorrection    = 0x78;
            constexpr uptr OrientationCorrection = 0x84;
            constexpr uptr Size                  = 0xE8;
        }

        namespace Lens // Cinemachine.LensSettings, relative to State + Lens
        {
            constexpr uptr FieldOfView           = 0x00;
            constexpr uptr NearClip              = 0x08;
            constexpr uptr FarClip               = 0x0C;
            constexpr uptr ModeOverride          = 0x14;
        }

        namespace VirtualCamera // CinemachineVirtualCamera
        {
            constexpr uptr LookAt                = 0xA8;
            constexpr uptr Follow                = 0xB0;
            constexpr uptr Lens                  = 0xB8; // LensSettings
            constexpr uptr State                 = 0x108; // CameraState
            constexpr uptr ComponentPipeline     = 0x1F0;
        }

        namespace AimingCameraFollow // CinemachineTPSO.CinemachineAimingCameraFollow
        {
            constexpr uptr FieldOfViewAimed      = 0x80;
            constexpr uptr WeightAiming          = 0xAC;
            constexpr uptr PreviousFollowTarget  = 0xB4; // Vector3 (local player)
            constexpr uptr CameraDistance        = 0xD0;
        }
    }

    // -------------------------------------------------------------------------
    // Native Unity 6 (6000.3) Transform layout. Reverse engineered from
    // UnityPlayer.dll:
    //   Transform::get_localPosition_Injected -> 0x18066a810
    //   Transform::get_localRotation_Injected -> 0x1800ccbd0
    //   Transform::get_position_Injected      -> 0x1806684f0
    //
    // The managed UnityEngine.Object stores the native object in m_CachedPtr
    // (0x10). The native Transform keeps a pointer to the shared transform
    // hierarchy plus a node index; the hierarchy holds one 0x30-byte record per
    // node (local position / rotation / scale) and an int32 parent index array
    // (-1 terminates the chain).
    // -------------------------------------------------------------------------
    namespace unity_native
    {
        constexpr uptr Object_CachedPtr          = 0x10; // managed -> native

        constexpr uptr Transform_Hierarchy       = 0x28;
        constexpr uptr Transform_Index           = 0x30; // int32

        constexpr uptr Hierarchy_Records         = 0x18; // node record array
        constexpr uptr Hierarchy_Parents         = 0x20; // int32[] parent index

        constexpr uptr Record_Stride             = 0x30;
        constexpr uptr Record_Position           = 0x00; // Vector3
        constexpr uptr Record_Rotation           = 0x10; // Quaternion
        constexpr uptr Record_Scale              = 0x20; // Vector3
    }
}
