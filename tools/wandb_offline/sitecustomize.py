"""Put on PYTHONPATH by tools/train_smoke.py and tools/bench_null.py only: their trainer runs never reach wandb.ai.

The fork's WandbWriter calls wandb.setup(wandb.Settings(...)), whose default mode ("online") overrides
WANDB_MODE, then wandb.init(...). Forcing mode="offline" in wandb.init keeps the writer (TensorBoard and console
writers come from the same list, and a failing wandb.init drops them all) while nothing leaves the machine.
"""

try:
    import wandb

    _init = wandb.init

    def _offline_init(*args, **kwargs):
        kwargs["mode"] = "offline"
        return _init(*args, **kwargs)

    wandb.init = _offline_init
except ImportError:
    pass
