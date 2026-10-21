# Native fbjni code looks up these names, methods and result fields by string.
# Library minifyEnabled=false does not protect them from host-app R8 shrinking.
-keep class xyz.bczl.io.IOHttpClient { *; }
-keep class xyz.bczl.io.IOHttpClient$* { *; }
