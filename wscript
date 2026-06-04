def options(opt):
    opt.load('pebble_sdk')

def configure(conf):
    conf.load('pebble_sdk')

def build(bld):
    bld.load('pebble_sdk')

    bld.pbl_bundle(
        sources=[
            'src/c/mdbl.c',
            'src/c/modules/health_relay.c',
        ],
        js=['src/pkjs/index.js'],
    )
