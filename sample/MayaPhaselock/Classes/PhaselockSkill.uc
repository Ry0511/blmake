class PhaselockSkill extends Skill;

var float MaxRange;
var float MinAimAlignment;
var PhaselockActor LiftActor;

function WillowAIPawn FindTarget(WillowPlayerController WPC) {
    local Vector ViewLocation;
    local Rotator ViewRotation;
    local WillowAIPawn P, Best;
    local float Alignment, BestAlignment;

    WPC.GetPlayerViewPoint(ViewLocation, ViewRotation);

    BestAlignment = MinAimAlignment;
    foreach WPC.Pawn.CollidingActors(class'WillowAIPawn', P, MaxRange, ViewLocation) {
        Alignment = Normal(P.Location - ViewLocation) Dot Vector(ViewRotation);

        if (Alignment > BestAlignment
            && P.IsAliveAndWell()
            && WPC.Pawn.IsEnemy(P)
            && WPC.Pawn.FastTrace(P.Location, ViewLocation)
        ) {
            Best = P;
            BestAlignment = Alignment;
        }
    }
    return Best;
}

event bool Activate() {
    local WillowPlayerController WPC;
    local WillowAIPawn Target;

    WPC = WillowPlayerController(SkillInstigator);
    if (WPC == none || WPC.Pawn == none) {
        return false;
    }

    Target = FindTarget(WPC);
    if (Target == none || !super.Activate()) {
        return false;
    }

    LiftActor = WPC.Spawn(class'PhaselockActor', WPC.Pawn);
    LiftActor.Lock(Target);
    return true;
}

event Deactivate() {
    if (LiftActor != none) {
        LiftActor.Destroy();
        LiftActor = none;
    }
    super.Deactivate();
}

defaultproperties
{
    MaxRange=2500.0
    MinAimAlignment=0.99
}
