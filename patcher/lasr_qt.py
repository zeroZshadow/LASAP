"""Stands in for PySide6.QtCore, which LASR's generator imports for its GUI thread and signals. The patcher runs the
generator on its own thread and connects to its signals directly, so plain Python does: the patcher needs no Qt."""
import sys
import types


class Signal:
    def __init__(self, *types_) -> None:
        self.name = ""

    def __set_name__(self, owner, name: str) -> None:
        self.name = name

    def __get__(self, instance, owner):
        if instance is None:
            return self
        bound = instance.__dict__.get(self.name)
        if bound is None:
            bound = instance.__dict__[self.name] = BoundSignal()
        return bound


class BoundSignal:
    def __init__(self) -> None:
        self.slots = []

    def connect(self, slot) -> None:
        self.slots.append(slot)

    def emit(self, *args) -> None:
        for slot in self.slots:
            slot(*args)


class QThread:
    def __init__(self, parent=None) -> None:
        pass


def install() -> None:
    qtcore = types.ModuleType("PySide6.QtCore")
    vars(qtcore).update(QThread=QThread, Signal=Signal)
    pyside = types.ModuleType("PySide6")
    vars(pyside).update(QtCore=qtcore)
    sys.modules["PySide6"] = pyside
    sys.modules["PySide6.QtCore"] = qtcore
