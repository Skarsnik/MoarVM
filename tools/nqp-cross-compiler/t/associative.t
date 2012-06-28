#!nqp
use MASTTesting;

plan(5);

sub hash_type($frame) {
    my @ins := $frame.instructions;
    my $r0 := local($frame, str);
    my $r1 := local($frame, NQPMu);
    my $r2 := local($frame, NQPMu);
    op(@ins, 'const_s', $r0, sval('MVMHash'));
    op(@ins, 'knowhow', $r1);
    op(@ins, 'findmeth', $r2, $r1, sval('new_type'));
    call(@ins, $r2, [$Arg::obj, $Arg::named +| $Arg::str], $r1, sval('repr'), $r0, :result($r1));
    $r1
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'elemskeyed', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "0\n",
    "New hash has zero elements");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'elemskeyed', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "1\n",
    "Adding to hash increases element count");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'const_s', $r2, sval('bar'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'elemskeyed', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "2\n",
    "Storage is actually based on the key");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'const_s', $r2, sval('bar'));
        op(@ins, 'existskey', $r1, $r0, $r2);
        op(@ins, 'say_i', $r1);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'existskey', $r1, $r0, $r2);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "0\n1\n",
    "Exists works");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'elemskeyed', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'deletekey', $r0, $r2);
        op(@ins, 'elemskeyed', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "1\n0\n",
    "Delete works");
