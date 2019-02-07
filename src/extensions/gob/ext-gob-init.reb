REBOL [
    Title: "GOB! Extension"
    Name: Gob
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}

    Notes: {
        See %extensions/gob/README.md
    }
]

; !!! Should call UNREGISTER-GOB-HOOKS at some point (module finalizer?)
;
register-gob-hooks

sys/export []  ; current hacky mechanism is to put any exports here