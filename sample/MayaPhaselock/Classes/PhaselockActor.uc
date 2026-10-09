class PhaselockActor extends Actor
    notplaceable;

var float LiftHeight;
var float LiftTime;
var WillowAIPawn Target;
var GearboxMind TargetMind;
var EPhysics TargetPhysics;
var Vector LiftStart;
var float LiftStartTime;
var string PhaseWalkPath;
var PhaseWalkDefinition PhaseWalk;
var array<MaterialInterface> SavedMaterials;
var AudioComponent LoopAudio;

function Lock(WillowAIPawn NewTarget) {
    local int I;

    Target = NewTarget;
    TargetMind = GearboxMind(Target.Controller);
    TargetPhysics = Target.Physics;
    LiftStart = Target.Location;
    LiftStartTime = WorldInfo.TimeSeconds;
    Target.StopWeaponFiring();

    PhaseWalk = PhaseWalkDefinition(DynamicLoadObject(PhaseWalkPath, class'PhaseWalkDefinition', true));
    if (PhaseWalk == none) {
        return;
    }

    for (I = 0; I < Target.Mesh.GetNumElements(); I++) {
        SavedMaterials[I] = Target.Mesh.GetMaterial(I);
        Target.Mesh.SetMaterial(I, PhaseWalk.PhaseWalkActiveMaterial);
    }
    Target.ReattachComponent(Target.Mesh);

    Target.PlaySound(PhaseWalk.BeginSound);
    LoopAudio = Target.CreateAudioComponent(PhaseWalk.LoopingSound, true, true);
}

event Tick(float DeltaTime) {
    local float Alpha;

    if (Target == none) {
        return;
    }

    if (Target.bDeleteMe || !Target.IsAliveAndWell()) {
        Release();
        return;
    }

    if (Target.Physics != PHYS_None) {
        Target.SetPhysics(PHYS_None);
    }

    Target.bNoWeaponFiring = true;
    if (TargetMind != none) {
        TargetMind.bDisabledDueToPopulationIrrelevance = true;
    }

    Alpha = FMin((WorldInfo.TimeSeconds - LiftStartTime) / LiftTime, 1.0);
    Target.Move(LiftStart + vect(0.0, 0.0, 1.0) * (LiftHeight * Alpha) - Target.Location);
}

function Release() {
    local int I;

    if (Target == none) {
        return;
    }

    if (PhaseWalk != none && !Target.bDeleteMe) {
        for (I = 0; I < SavedMaterials.Length; I++) {
            if (Target.Mesh.GetMaterial(I) == PhaseWalk.PhaseWalkActiveMaterial) {
                Target.Mesh.SetMaterial(I, SavedMaterials[I]);
            }
        }
        Target.ReattachComponent(Target.Mesh);
        Target.PlaySound(PhaseWalk.EndSound);
    }

    if (LoopAudio != none) {
        LoopAudio.FadeOut(0.25, 0.0);
        LoopAudio = none;
    }
    SavedMaterials.Length = 0;

    if (TargetMind != none) {
        TargetMind.bDisabledDueToPopulationIrrelevance = false;
    }

    Target.bNoWeaponFiring = false;
    if (!Target.bDeleteMe && Target.IsAliveAndWell()) {
        if (TargetPhysics == PHYS_Walking) {
            TargetPhysics = PHYS_Falling;
        }
        Target.SetPhysics(TargetPhysics);
    }
    Target = none;
}

event Destroyed() {
    Release();
    super.Destroyed();
}

defaultproperties
{
    LiftHeight=160.0
    LiftTime=0.6
    PhaseWalkPath="gd_lilith.Skills.PhaseWalkDef"
    bHidden=true
    RemoteRole=ROLE_None
}
