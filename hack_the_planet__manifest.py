App(
    appid="hack_the_planet",
    name="Hack the Planet",
    apptype=FlipperAppType.EXTERNAL,
    entry_point="hack_the_planet_app",
    stack_size=2 * 1024,
    fap_category="GPIO",
    fap_description="Monitor plant bioelectrical signals and convert to audio",
    fap_author="Your Name",
    fap_version="1.0",
    fap_icon="hack_the_planet.png",
    fap_libs=["assets"],
    requires=[
        "gui",
        "notification",
        "speaker",
        "adc",
    ],
    provides=[
        "hack_the_planet",
    ],
)
