# SPDX-License-Identifier: 0BSD
# awk -f summarize.awk comparison/micro.csv comparison/applications.csv
BEGIN { FS = "," }
FNR == 1 { next }
{
    micro = FILENAME ~ /micro.csv$/
    group = $1 SUBSEP (micro ? "micro" : $2)
    version = micro ? $2 : $3
    key = group SUBSEP version
    value[key, ++count[key]] = micro ? $7 : $5
    if (!(group in seen)) {
        seen[group] = 1
        groups[++ngroups] = group
        benchmark[group] = $1
        mode[group] = micro ? "micro" : $2
        unit[group] = micro ? "iterations/s" : "seconds"
    }
}
function median(key,    n,i,j,x) {
    n = count[key]
    for (i=2; i<=n; i++) {
        x = value[key,i]
        for (j=i-1; j>0 && value[key,j]>x; j--) value[key,j+1] = value[key,j]
        value[key,j+1] = x
    }
    return n % 2 ? value[key,(n+1)/2] : (value[key,n/2] + value[key,n/2+1])/2
}
END {
    print "benchmark,mode,unit,original_median,generated_median,change_pct,original_min,original_max,generated_min,generated_max"
    for (i=1; i<=ngroups; i++) {
        g=groups[i]; old=g SUBSEP "original"; new=g SUBSEP "generated"
        a=median(old); b=median(new)
        if (!a || !b) exit 1
        printf "%s,%s,%s,%.3f,%.3f,%+.2f,%.3f,%.3f,%.3f,%.3f\n", \
            benchmark[g],mode[g],unit[g],a,b,100*(b/a-1), \
            value[old,1],value[old,count[old]],value[new,1],value[new,count[new]]
    }
}
