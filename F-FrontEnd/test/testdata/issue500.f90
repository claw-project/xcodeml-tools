MODULE m
INTERFACE
 FUNCTION f()
  TYPE, BIND(C) :: t
   INTEGER :: v
  END TYPE
  TYPE(t) :: f
 END FUNCTION
END INTERFACE
END

PROGRAM main
  TYPE, BIND(C) :: t
   INTEGER :: v
  END TYPE
END PROGRAM main
